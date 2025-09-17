//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadPlanner.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

#include <memory>
#include <vector>

using oxygen::engine::upload::UploadBufferDesc;
using oxygen::engine::upload::UploadError;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadPlanner;
using oxygen::engine::upload::UploadPolicy;
using oxygen::engine::upload::UploadRequest;

namespace {

//=== Dummy Buffer for testing -----------------------------------------------//

// Minimal dummy buffer to test buffer upload planning
class DummyBuffer : public oxygen::graphics::Buffer {
public:
  explicit DummyBuffer(const oxygen::graphics::BufferDesc& d)
    : Buffer("DummyBuf")
    , desc_(d)
  {
  }
  auto GetDescriptor() const noexcept -> oxygen::graphics::BufferDesc override
  {
    return desc_;
  }
  auto GetNativeResource() const -> oxygen::graphics::NativeResource override
  {
    return oxygen::graphics::NativeResource(
      const_cast<DummyBuffer*>(this), ClassTypeId());
  }

  // Minimal implementations of pure virtuals to make this a concrete type.
  auto Update(const void* /*data*/, uint64_t /*size*/, uint64_t /*offset*/)
    -> void override
  {
  }
  [[nodiscard]] auto GetSize() const noexcept -> uint64_t override
  {
    return desc_.size_bytes;
  }
  [[nodiscard]] auto GetUsage() const noexcept
    -> oxygen::graphics::BufferUsage override
  {
    return desc_.usage;
  }
  [[nodiscard]] auto GetMemoryType() const noexcept
    -> oxygen::graphics::BufferMemory override
  {
    return desc_.memory;
  }
  [[nodiscard]] auto IsMapped() const noexcept -> bool override
  {
    return false;
  }
  [[nodiscard]] auto GetGPUVirtualAddress() const -> uint64_t override
  {
    return 0;
  }

protected:
  auto DoMap(uint64_t /*offset*/, uint64_t /*size*/) -> void* override
  {
    return nullptr;
  }
  auto DoUnMap() noexcept -> void override { }
  [[nodiscard]] auto CreateConstantBufferView(
    const oxygen::graphics::DescriptorHandle&,
    const oxygen::graphics::BufferRange&) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateShaderResourceView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::BufferRange, uint32_t) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateUnorderedAccessView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::BufferRange, uint32_t) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }

private:
  oxygen::graphics::BufferDesc desc_;
};

//===----------------------------------------------------------------------===//
// Tests for UploadPlanner buffer upload logic
//===----------------------------------------------------------------------===//

//! Fixture for buffer upload planning tests.
/*!
 Provides common helpers and default setup for buffer-related upload tests.

 Key responsibilities:
 - Create dummy buffers used as upload destinations
 - Provide helper to construct UploadRequest instances

 This keeps per-test state isolated and avoids globals.
*/
// Base fixture with helpers shared by specialized fixtures.
class UploadPlannerBufferTest : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }

  // Helper to create a move-friendly UploadRequest for buffer uploads.
  auto MakeBufferUpload(const std::shared_ptr<DummyBuffer>& dst,
    uint64_t size_bytes, uint64_t dst_offset) -> UploadRequest
  {
    UploadRequest r;
    r.kind = UploadKind::kBuffer;
    r.desc = UploadBufferDesc {
      .dst = dst, .size_bytes = size_bytes, .dst_offset = dst_offset
    };
    return r;
  }

  // Convenience helper to create a dummy buffer with given size (default 4096).
  auto MakeDummyBuffer(uint64_t size_bytes = 4096ull)
    -> std::shared_ptr<DummyBuffer>
  {
    oxygen::graphics::BufferDesc bd;
    bd.size_bytes = size_bytes;
    return std::make_shared<DummyBuffer>(bd);
  }
};

//! Ensure OptimizeBuffers returns an empty plan when given an empty plan.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_EmptyPlanReturnsEmpty)
{
  // Arrange: empty requests and empty plan
  std::vector<UploadRequest> reqs;
  oxygen::engine::upload::BufferUploadPlan empty_plan;

  // Act
  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, empty_plan, UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Assert: empty plan preserved
  EXPECT_TRUE(out.uploads.empty());
  EXPECT_EQ(out.total_bytes, 0u);
}

//! PlanBuffers should pack buffer uploads and align staging offsets.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferPlan_PackingAndAlignment)
{
  // Arrange: create a dummy destination buffer of 4096 bytes
  auto buf = MakeDummyBuffer();

  // Two requests: sizes 100 and 200 bytes, different dst_offset
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 100, 0));
  reqs.emplace_back(MakeBufferUpload(buf, 200, 100));

  // Act
  const auto exp_plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});

  // Assert
  ASSERT_HAS_VALUE(exp_plan);
  const auto& plan = exp_plan.value();
  ASSERT_EQ(plan.uploads.size(), 2u);

  const auto& it0 = plan.uploads[0];
  const auto& it1 = plan.uploads[1];

  // src offsets must be aligned to policy (kBufferCopyAlignment = 512)
  EXPECT_EQ(it0.region.src_offset
      % UploadPolicy::AlignmentPolicy::kBufferCopyAlignment.get(),
    0u);
  EXPECT_EQ(it1.region.src_offset
      % UploadPolicy::AlignmentPolicy::kBufferCopyAlignment.get(),
    0u);

  // Regions preserve dst offsets and sizes
  EXPECT_EQ(it0.region.dst_offset, 0u);
  EXPECT_EQ(it0.region.size, 100u);
  EXPECT_EQ(it1.region.dst_offset, 100u);
  EXPECT_EQ(it1.region.size, 200u);

  // total_bytes should be at least src_offset + size of last
  EXPECT_GE(plan.total_bytes, it1.region.src_offset + it1.region.size);
}

//! OptimizeBuffers should coalesce contiguous src/dst regions targeting the
//! same buffer.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_CoalesceContiguous)
{
  // Arrange: create a dummy buffer
  auto buf = MakeDummyBuffer();

  // Two requests that are contiguous in dst and will be contiguous in src
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 256, 0));
  reqs.emplace_back(MakeBufferUpload(buf, 256, 256));

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  // Act: optimize
  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, plan.value(), UploadPolicy {});

  // Assert
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Expect coalesced into single upload
  ASSERT_EQ(out.uploads.size(), 1u);
  const auto& merged = out.uploads[0];
  EXPECT_EQ(merged.region.dst_offset, 0u);
  EXPECT_EQ(merged.region.size, 512u);
  EXPECT_EQ(merged.request_indices.size(), 2u);
}

//! Do not merge when source staging offsets are non-contiguous.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_NonContiguousSrcNotMerged)
{
  auto buf = MakeDummyBuffer();

  // Two requests contiguous in dst but we will simulate non-contiguous src by
  // relying on PlanBuffers alignment gaps (create sizes that cause alignment to
  // insert a hole between src offsets)
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 100, 0));
  reqs.emplace_back(MakeBufferUpload(buf, 200, 100));

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  // Manually perturb plan to create non-contiguous src offsets while dst is
  // contiguous
  auto changed = plan.value();
  // introduce a gap: set second src_offset to first.src_offset + first.size +
  // 512
  changed.uploads[1].region.src_offset = changed.uploads[0].region.src_offset
    + changed.uploads[0].region.size + 512;

  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, changed, UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Should NOT merge because src are non-contiguous
  EXPECT_EQ(out.uploads.size(), 2u);
}

//! Do not merge when destination offsets are non-contiguous even if source
//! regions are contiguous.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_NonContiguousDstNotMerged)
{
  // Arrange: sizes aligned so src will be contiguous, but dst offsets leave a
  // gap
  auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 512, 0));
  reqs.emplace_back(MakeBufferUpload(buf, 512, 600)); // dst not contiguous

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  // Act: optimize
  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, plan.value(), UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Should NOT merge because dst offsets are not contiguous
  EXPECT_EQ(out.uploads.size(), 2u);
}

//! Do not merge regions that target different destination buffers.
NOLINT_TEST_F(
  UploadPlannerBufferTest, BufferOptimize_DifferentDestinationNotMerged)
{
  auto buf1 = MakeDummyBuffer();
  auto buf2 = MakeDummyBuffer();

  // Two requests contiguous in src/dst offsets but target different buffers
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf1, 256, 0));
  reqs.emplace_back(MakeBufferUpload(buf2, 256, 0));

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, plan.value(), UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Should NOT merge because destinations differ
  EXPECT_EQ(out.uploads.size(), 2u);
}

//! Chain-merge aligned, contiguous requests into a single upload and preserve
//! total_bytes.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_ChainMergeThreeRequests)
{
  // Arrange: use sizes aligned to kBufferCopyAlignment so src offsets are
  // contiguous after PlanBuffers. Alignment is 512.
  auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 512, 0));
  reqs.emplace_back(MakeBufferUpload(buf, 512, 512));
  reqs.emplace_back(MakeBufferUpload(buf, 512, 1024));

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  // Act: optimize
  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, plan.value(), UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Assert: all three requests coalesced into a single upload
  ASSERT_EQ(out.uploads.size(), 1u);
  const auto& merged = out.uploads[0];
  EXPECT_EQ(merged.region.dst_offset, 0u);
  EXPECT_EQ(merged.region.size, 512u * 3u);
  EXPECT_EQ(merged.request_indices.size(), 3u);
  // total_bytes preserved
  EXPECT_EQ(out.total_bytes, plan->total_bytes);
}

//! Verify merged request_indices reflect sorted destination order after
//! planning.
NOLINT_TEST_F(UploadPlannerBufferTest,
  BufferOptimize_MergedRequestIndicesPreserveSortedDstOrder)
{
  // Arrange: create a single destination buffer and three requests inserted in
  // reverse destination order (to exercise PlanBuffers sorting).
  auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> reqs;
  // index 0 -> highest dst offset
  reqs.emplace_back(MakeBufferUpload(buf, 512, 1024));
  // index 1 -> middle dst offset
  reqs.emplace_back(MakeBufferUpload(buf, 512, 512));
  // index 2 -> lowest dst offset
  reqs.emplace_back(MakeBufferUpload(buf, 512, 0));

  // Act: plan and optimize
  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);
  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, plan.value(), UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Assert: all three coalesced into one upload
  ASSERT_EQ(out.uploads.size(), 1u);
  const auto& merged = out.uploads[0];
  ASSERT_EQ(merged.request_indices.size(), 3u);

  // Because PlanBuffers sorts by dst_offset ascending, the representative
  // ordering inside request_indices should be {2,1,0} (original indices)
  EXPECT_EQ(merged.request_indices[0], 2u);
  EXPECT_EQ(merged.request_indices[1], 1u);
  EXPECT_EQ(merged.request_indices[2], 0u);
}

//! When inputs are already ordered by dst, merged request_indices should
//! preserve that input order.
NOLINT_TEST_F(UploadPlannerBufferTest,
  BufferOptimize_MergedRequestIndicesPreserveInputOrderWhenAlreadyOrdered)
{
  // Arrange: requests already in ascending dst_offset order.
  auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 512, 0));
  reqs.emplace_back(MakeBufferUpload(buf, 512, 512));
  reqs.emplace_back(MakeBufferUpload(buf, 512, 1024));

  // Act: plan and optimize
  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);
  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, plan.value(), UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Assert: merged and request_indices preserve the original index order
  ASSERT_EQ(out.uploads.size(), 1u);
  const auto& merged = out.uploads[0];
  ASSERT_EQ(merged.request_indices.size(), 3u);
  EXPECT_EQ(merged.request_indices[0], 0u);
  EXPECT_EQ(merged.request_indices[1], 1u);
  EXPECT_EQ(merged.request_indices[2], 2u);
}

//! Ensure merged uploads never mix requests from different destination buffers.
NOLINT_TEST_F(UploadPlannerBufferTest,
  BufferOptimize_RequestIndicesSeparateForDifferentBuffers)
{
  auto buf1 = MakeDummyBuffer();
  auto buf2 = MakeDummyBuffer();

  std::vector<UploadRequest> reqs;
  // index 0 -> buf1 @ 0
  reqs.emplace_back(MakeBufferUpload(buf1, 512, 0));
  // index 1 -> buf2 @ 0 (different buffer)
  reqs.emplace_back(MakeBufferUpload(buf2, 512, 0));
  // index 2 -> buf1 @ 512 (same as buf1, contiguous with index 0)
  reqs.emplace_back(MakeBufferUpload(buf1, 512, 512));

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, plan.value(), UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // We expect 2 uploads for buf1 (merged indices) and buf2 separate => total 2
  // Because PlanBuffers sorts by destination pointer then offset, the exact
  // order of uploads in the plan can place buf2 between buf1 groups; ensure
  // that no merged upload contains indices from different buffers.
  for (const auto& item : out.uploads) {
    // Each request_indices should all reference the same dst buffer
    const auto rep_idx = item.request_indices.front();
    const auto& rep_desc = std::get<UploadBufferDesc>(reqs[rep_idx].desc);
    for (auto idx : item.request_indices) {
      const auto& desc = std::get<UploadBufferDesc>(reqs[idx].desc);
      EXPECT_EQ(desc.dst.get(), rep_desc.dst.get());
    }
  }
}

//! Partial merges should form groups and preserve request_indices ordering
//! within each group.
NOLINT_TEST_F(
  UploadPlannerBufferTest, BufferOptimize_PartialMergesPreserveRequestIndices)
{
  // Arrange: single buffer with four requests where [0,1] are contiguous and
  // [2,3] are contiguous but separated by a gap between 1 and 2.
  auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 512, 0)); // idx 0
  reqs.emplace_back(MakeBufferUpload(buf, 512, 512)); // idx 1
  reqs.emplace_back(MakeBufferUpload(buf, 512, 2048)); // idx 2 (gap)
  reqs.emplace_back(
    MakeBufferUpload(buf, 512, 2560)); // idx 3 (contiguous with 2)

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  const auto opt
    = UploadPlanner::OptimizeBuffers(reqs, plan.value(), UploadPolicy {});
  ASSERT_HAS_VALUE(opt);
  const auto& out = opt.value();

  // Expect two merged uploads: one for indices {0,1} and one for {2,3}
  ASSERT_EQ(out.uploads.size(), 2u);

  // Find which upload contains idx 0 and which contains idx 2 and assert
  // groupings
  bool found01 = false, found23 = false;
  for (const auto& item : out.uploads) {
    const auto& inds = item.request_indices;
    if (inds.size() == 2 && inds[0] == 0u && inds[1] == 1u) {
      found01 = true;
    }
    if (inds.size() == 2 && inds[0] == 2u && inds[1] == 3u) {
      found23 = true;
    }
  }
  EXPECT_TRUE(found01);
  EXPECT_TRUE(found23);
}

//===----------------------------------------------------------------------===//
// Edge and Error Tests for UploadPlanner buffer upload logic
//===----------------------------------------------------------------------===//

class UploadPlannerBufferEdgeTest : public UploadPlannerBufferTest { };

//! Edge: zero-length buffer upload requests should be ignored by PlanBuffers
NOLINT_TEST_F(UploadPlannerBufferEdgeTest, BufferPlan_ZeroLengthIgnored)
{
  auto buf = MakeDummyBuffer();

  // A zero-length request and a normal request
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 0, 0));
  reqs.emplace_back(MakeBufferUpload(buf, 128, 0));

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  // Expect only the non-zero request to be planned
  EXPECT_EQ(plan->uploads.size(), 1u);
  EXPECT_EQ(plan->uploads[0].region.size, 128u);
}

//! PlanBuffers: non-empty span with all invalid requests returns error.
NOLINT_TEST_F(UploadPlannerBufferEdgeTest, BufferPlan_AllInvalid_ReturnsError)
{
  // Arrange
  std::vector<UploadRequest> reqs;
  // Invalid: null dst and zero size
  UploadRequest r0;
  r0.kind = UploadKind::kBuffer;
  r0.desc
    = UploadBufferDesc { .dst = nullptr, .size_bytes = 0, .dst_offset = 0 };
  reqs.emplace_back(std::move(r0));
  // Invalid: kind mismatch (e.g., texture) also considered invalid for
  // PlanBuffers
  UploadRequest r1;
  r1.kind = UploadKind::kTexture2D;
  reqs.emplace_back(std::move(r1));

  // Act
  const auto exp_plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});

  // Assert
  ASSERT_FALSE(exp_plan.has_value());
  EXPECT_EQ(exp_plan.error(), UploadError::kInvalidRequest);
}

//! Edge: misaligned dst offsets (not meeting placement align) should still
//! produce uploads but dst offsets are preserved; PlanBuffers should not adjust
//! dst_offset alignment (only src alignment matters for staging).
NOLINT_TEST_F(
  UploadPlannerBufferEdgeTest, BufferPlan_MisalignedDstOffsetPreserved)
{
  auto buf = MakeDummyBuffer();

  // Create a request with a dst_offset that is misaligned (e.g., 7)
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(MakeBufferUpload(buf, 64, 7));

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);

  // dst_offset in the planned region must match requested dst_offset
  ASSERT_EQ(plan->uploads.size(), 1u);
  EXPECT_EQ(plan->uploads[0].region.dst_offset, 7u);

  // But src_offset must respect staging alignment policy
  EXPECT_EQ(plan->uploads[0].region.src_offset
      % UploadPolicy::AlignmentPolicy::kBufferCopyAlignment.get(),
    0u);
}

} // namespace
