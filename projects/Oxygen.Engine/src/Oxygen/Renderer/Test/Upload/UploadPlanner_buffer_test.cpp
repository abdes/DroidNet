//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadPlanner.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

using oxygen::engine::upload::BufferUploadPlan;
using oxygen::engine::upload::UploadBufferDesc;
using oxygen::engine::upload::UploadError;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadPlanner;
using oxygen::engine::upload::UploadPolicy;
using oxygen::engine::upload::UploadRequest;

namespace {

//=== Dummy Buffer for testing -----------------------------------------------//

// Minimal dummy buffer to test buffer upload planning
class DummyBuffer final : public oxygen::graphics::Buffer {
public:
  explicit DummyBuffer(oxygen::graphics::BufferDesc d)
    : Buffer("DummyBuf")
    , desc_(std::move(d))
  {
  }
  auto GetDescriptor() const noexcept -> oxygen::graphics::BufferDesc override
  {
    return desc_;
  }
  auto GetNativeResource() const -> oxygen::graphics::NativeResource override
  {
    return { const_cast<DummyBuffer*>(this), ClassTypeId() };
  }

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
class UploadPlannerBufferTest : public testing::Test {
protected:
  auto SetUp() -> void override { }
  auto TearDown() -> void override { }

  // Helper to create a move-friendly UploadRequest for buffer uploads.
  static auto MakeBufferUpload(const std::shared_ptr<DummyBuffer>& dst,
    const uint64_t size_bytes, const uint64_t dst_offset) -> UploadRequest
  {
    UploadRequest r;
    r.kind = UploadKind::kBuffer;
    r.desc = UploadBufferDesc {
      .dst = dst, .size_bytes = size_bytes, .dst_offset = dst_offset
    };
    return r;
  }

  // Convenience helper to create a dummy buffer with given size (default 4096).
  static auto MakeDummyBuffer(const uint64_t size_bytes = 4096ull)
    -> std::shared_ptr<DummyBuffer>
  {
    oxygen::graphics::BufferDesc bd;
    bd.size_bytes = size_bytes;
    return std::make_shared<DummyBuffer>(bd);
  }

  auto UploadQueueKey() const
  {
    return oxygen::graphics::QueueKey("universal");
  }
};

//! Ensure OptimizeBuffers returns an empty plan when given an empty plan.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_EmptyPlanReturnsEmpty)
{
  // Arrange: empty requests and empty plan
  std::vector<UploadRequest> requests;
  const BufferUploadPlan empty_plan;

  // Act
  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, empty_plan, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // Assert: empty plan preserved
  EXPECT_TRUE(uploads.empty());
  EXPECT_EQ(total_bytes, 0u);
}

//! PlanBuffers should pack buffer uploads and align staging offsets.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferPlan_PackingAndAlignment)
{
  // Arrange: create a dummy destination buffer of 4096 bytes
  const auto buf = MakeDummyBuffer();

  // Two requests: sizes 100 and 200 bytes, different dst_offset
  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 100, 0));
  requests.emplace_back(MakeBufferUpload(buf, 200, 100));

  // Act
  const auto exp_plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));

  // Assert
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  ASSERT_EQ(plan.uploads.size(), 2u);

  const auto& it0 = plan.uploads[0];
  const auto& it1 = plan.uploads[1];

  // src offsets must be aligned to policy (buffer_copy_alignment)
  EXPECT_EQ(it0.region.src_offset
      % UploadPolicy(UploadQueueKey()).alignment.buffer_copy_alignment.get(),
    0u);
  EXPECT_EQ(it1.region.src_offset
      % UploadPolicy(UploadQueueKey()).alignment.buffer_copy_alignment.get(),
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
  const auto buf = MakeDummyBuffer();

  // Two requests that are contiguous in dst and will be contiguous in src
  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 256, 0));
  requests.emplace_back(MakeBufferUpload(buf, 256, 256));

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  // Act: optimize
  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, plan.value(), UploadPolicy(UploadQueueKey()));

  // Assert
  ASSERT_TRUE(opt.has_value());
  const auto& optimized = opt.value();
  const auto& uploads = optimized.uploads;

  // Expect coalesced into single upload
  ASSERT_EQ(uploads.size(), 1u);
  const auto& [region, request_indices] = uploads[0];
  EXPECT_EQ(region.dst_offset, 0u);
  EXPECT_EQ(region.size, 512u);
  EXPECT_EQ(request_indices.size(), 2u);
}

//! Do not merge when source staging offsets are non-contiguous.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_NonContiguousSrcNotMerged)
{
  const auto buf = MakeDummyBuffer();

  // Two requests contiguous in dst, but we will simulate non-contiguous src by
  // relying on PlanBuffers alignment gaps (create sizes that cause alignment to
  // insert a hole between src offsets)
  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 100, 0));
  requests.emplace_back(MakeBufferUpload(buf, 200, 100));

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  // Manually perturb plan to create non-contiguous src offsets while dst is
  // contiguous
  auto changed = plan.value();
  // introduce a gap: set second src_offset to first.src_offset + first.size +
  // 512
  changed.uploads[1].region.src_offset = changed.uploads[0].region.src_offset
    + changed.uploads[0].region.size + 512;

  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, changed, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // Should NOT merge because src are non-contiguous
  EXPECT_EQ(uploads.size(), 2u);
}

//! Do not merge when destination offsets are non-contiguous even if source
//! regions are contiguous.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_NonContiguousDstNotMerged)
{
  // Arrange: sizes aligned so src will be contiguous, but dst offsets leave a
  // gap
  const auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 512, 0));
  requests.emplace_back(MakeBufferUpload(buf, 512, 600)); // dst not contiguous

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  // Act: optimize
  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, plan.value(), UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // Should NOT merge because dst offsets are not contiguous
  EXPECT_EQ(uploads.size(), 2u);
}

//! Do not merge regions that target different destination buffers.
NOLINT_TEST_F(
  UploadPlannerBufferTest, BufferOptimize_DifferentDestinationNotMerged)
{
  const auto buf1 = MakeDummyBuffer();
  const auto buf2 = MakeDummyBuffer();

  // Two requests contiguous in src/dst offsets but target different buffers
  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf1, 256, 0));
  requests.emplace_back(MakeBufferUpload(buf2, 256, 0));

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, plan.value(), UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // Should NOT merge because destinations differ
  EXPECT_EQ(uploads.size(), 2u);
}

//! Chain-merge aligned, contiguous requests into a single upload and preserve
//! total_bytes.
NOLINT_TEST_F(UploadPlannerBufferTest, BufferOptimize_ChainMergeThreeRequests)
{
  // Arrange: use sizes aligned to kBufferCopyAlignment so src offsets are
  // contiguous after PlanBuffers. Alignment is 512.
  const auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 512, 0));
  requests.emplace_back(MakeBufferUpload(buf, 512, 512));
  requests.emplace_back(MakeBufferUpload(buf, 512, 1024));

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  // Act: optimize
  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, plan.value(), UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // Assert: all three requests coalesced into a single upload
  ASSERT_EQ(uploads.size(), 1u);
  const auto& [region, request_indices] = uploads[0];
  EXPECT_EQ(region.dst_offset, 0u);
  EXPECT_EQ(region.size, 512u * 3u);
  EXPECT_EQ(request_indices.size(), 3u);
  // total_bytes preserved
  EXPECT_EQ(total_bytes, plan->total_bytes);
}

//! Verify merged request_indices reflect sorted destination order after
//! planning.
NOLINT_TEST_F(UploadPlannerBufferTest,
  BufferOptimize_MergedRequestIndicesPreserveSortedDstOrder)
{
  // Arrange: create a single destination buffer and three requests inserted in
  // reverse destination order (to exercise PlanBuffers sorting).
  const auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> requests;
  // index 0 -> highest dst offset
  requests.emplace_back(MakeBufferUpload(buf, 512, 1024));
  // index 1 -> middle dst offset
  requests.emplace_back(MakeBufferUpload(buf, 512, 512));
  // index 2 -> lowest dst offset
  requests.emplace_back(MakeBufferUpload(buf, 512, 0));

  // Act: plan and optimize
  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());
  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, plan.value(), UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // Assert: all three coalesced into one upload
  ASSERT_EQ(uploads.size(), 1u);
  const auto& [region, request_indices] = uploads[0];
  ASSERT_EQ(request_indices.size(), 3u);

  // Because PlanBuffers sorts by dst_offset ascending, the representative
  // ordering inside request_indices should be {2,1,0} (original indices)
  EXPECT_EQ(request_indices[0], 2u);
  EXPECT_EQ(request_indices[1], 1u);
  EXPECT_EQ(request_indices[2], 0u);
}

//! When inputs are already ordered by dst, merged request_indices should
//! preserve that input order.
NOLINT_TEST_F(UploadPlannerBufferTest,
  BufferOptimize_MergedRequestIndicesPreserveInputOrderWhenAlreadyOrdered)
{
  // Arrange: requests already in ascending dst_offset order.
  const auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 512, 0));
  requests.emplace_back(MakeBufferUpload(buf, 512, 512));
  requests.emplace_back(MakeBufferUpload(buf, 512, 1024));

  // Act: plan and optimize
  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());
  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, plan.value(), UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // Assert: merged and request_indices preserve the original index order
  ASSERT_EQ(uploads.size(), 1u);
  const auto& [region, request_indices] = uploads[0];
  ASSERT_EQ(request_indices.size(), 3u);
  EXPECT_EQ(request_indices[0], 0u);
  EXPECT_EQ(request_indices[1], 1u);
  EXPECT_EQ(request_indices[2], 2u);
}

//! Ensure merged uploads never mix requests from different destination buffers.
NOLINT_TEST_F(UploadPlannerBufferTest,
  BufferOptimize_RequestIndicesSeparateForDifferentBuffers)
{
  const auto buf1 = MakeDummyBuffer();
  const auto buf2 = MakeDummyBuffer();

  std::vector<UploadRequest> requests;
  // index 0 -> buf1 @ 0
  requests.emplace_back(MakeBufferUpload(buf1, 512, 0));
  // index 1 -> buf2 @ 0 (different buffer)
  requests.emplace_back(MakeBufferUpload(buf2, 512, 0));
  // index 2 -> buf1 @ 512 (same as buf1, contiguous with index 0)
  requests.emplace_back(MakeBufferUpload(buf1, 512, 512));

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, plan.value(), UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // We expect 2 uploads for buf1 (merged indices) and buf2 separate => total 2
  // Because PlanBuffers sorts by destination pointer then offset, the exact
  // order of uploads in the plan can place buf2 between buf1 groups; ensure
  // that no merged upload contains indices from different buffers.
  for (const auto& [region, request_indices] : uploads) {
    // Each request_indices should all reference the same dst buffer
    const auto rep_idx = request_indices.front();
    const auto& rep_desc = std::get<UploadBufferDesc>(requests[rep_idx].desc);
    for (const auto idx : request_indices) {
      const auto& desc = std::get<UploadBufferDesc>(requests[idx].desc);
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
  const auto buf = MakeDummyBuffer();

  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 512, 0)); // idx 0
  requests.emplace_back(MakeBufferUpload(buf, 512, 512)); // idx 1
  requests.emplace_back(MakeBufferUpload(buf, 512, 2048)); // idx 2 (gap)
  requests.emplace_back(
    MakeBufferUpload(buf, 512, 2560)); // idx 3 (contiguous with 2)

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  const auto opt = UploadPlanner::OptimizeBuffers(
    requests, plan.value(), UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(opt.has_value());
  const auto& [uploads, total_bytes] = opt.value();

  // Expect two merged uploads: one for indices {0,1} and one for {2,3}
  ASSERT_EQ(uploads.size(), 2u);

  // Find which upload contains idx 0 and which contains idx 2 and assert
  // groupings
  bool found01 = false, found23 = false;
  for (const auto& [region, request_indices] : uploads) {
    if (request_indices.size() == 2 && request_indices[0] == 0u
      && request_indices[1] == 1u) {
      found01 = true;
    }
    if (request_indices.size() == 2 && request_indices[0] == 2u
      && request_indices[1] == 3u) {
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
  const auto buf = MakeDummyBuffer();

  // A zero-length request and a normal request
  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 0, 0));
  requests.emplace_back(MakeBufferUpload(buf, 128, 0));

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  // Expect only the non-zero request to be planned
  EXPECT_EQ(plan->uploads.size(), 1u);
  EXPECT_EQ(plan->uploads[0].region.size, 128u);
}

//! PlanBuffers: non-empty span with all invalid requests returns error.
NOLINT_TEST_F(UploadPlannerBufferEdgeTest, BufferPlan_AllInvalid_ReturnsError)
{
  // Arrange
  std::vector<UploadRequest> requests;
  // Invalid: null dst and zero size
  UploadRequest r0;
  r0.kind = UploadKind::kBuffer;
  r0.desc
    = UploadBufferDesc { .dst = nullptr, .size_bytes = 0, .dst_offset = 0 };
  requests.emplace_back(std::move(r0));
  // Invalid: kind mismatch (e.g., texture) also considered invalid for
  // PlanBuffers
  UploadRequest r1;
  r1.kind = UploadKind::kTexture2D;
  requests.emplace_back(std::move(r1));

  // Act
  const auto exp_plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));

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
  const auto buf = MakeDummyBuffer();

  // Create a request with a dst_offset that is misaligned (e.g., 7)
  std::vector<UploadRequest> requests;
  requests.emplace_back(MakeBufferUpload(buf, 64, 7));

  const auto plan
    = UploadPlanner::PlanBuffers(requests, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(plan.has_value());

  // dst_offset in the planned region must match requested dst_offset
  ASSERT_EQ(plan->uploads.size(), 1u);
  EXPECT_EQ(plan->uploads[0].region.dst_offset, 7u);

  // But src_offset must respect staging alignment policy
  EXPECT_EQ(plan->uploads[0].region.src_offset
      % UploadPolicy(UploadQueueKey()).alignment.buffer_copy_alignment.get(),
    0u);
}

} // namespace
