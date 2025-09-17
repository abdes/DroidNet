//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
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
using oxygen::engine::upload::UploadSubresource;
using oxygen::engine::upload::UploadTextureDesc;

namespace {

// Minimal dummy buffer used only for planning verification. Similar to
// helpers in other tests but kept local to avoid fixture creation.
class LocalDummyBuffer : public oxygen::graphics::Buffer {
public:
  explicit LocalDummyBuffer(const oxygen::graphics::BufferDesc& d)
    : Buffer("LocalDummyBuf")
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
      const_cast<LocalDummyBuffer*>(this), ClassTypeId());
  }

protected:
  // Minimal implementations of abstract methods
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

// Minimal dummy texture for a tiny set of texture-plan tests.
class LocalDummyTexture : public oxygen::graphics::Texture {
public:
  explicit LocalDummyTexture(const oxygen::graphics::TextureDesc& d)
    : Texture("LocalDummyTex")
    , desc_(d)
  {
  }
  auto GetDescriptor() const -> const oxygen::graphics::TextureDesc& override
  {
    return desc_;
  }
  auto GetNativeResource() const -> oxygen::graphics::NativeResource override
  {
    return oxygen::graphics::NativeResource(
      const_cast<LocalDummyTexture*>(this), ClassTypeId());
  }

protected:
  [[nodiscard]] auto CreateShaderResourceView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::TextureType, oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateUnorderedAccessView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::TextureType, oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateRenderTargetView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateDepthStencilView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::TextureSubResourceSet, bool) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }

private:
  oxygen::graphics::TextureDesc desc_;
};

//! PlanBuffers: empty request span should produce an empty but valid plan.
NOLINT_TEST(UploadPlannerBasicTest, BufferPlan_EmptyRequestsReturnsEmptyPlan)
{
  std::vector<UploadRequest> reqs; // empty
  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});

  // Should return a valid plan with no uploads and total_bytes == 0
  ASSERT_HAS_VALUE(plan);
  const auto& p = plan.value();
  EXPECT_TRUE(p.uploads.empty());
  EXPECT_EQ(p.total_bytes, 0u);
}

//! PlanBuffers: requests for the same dst buffer must be ordered by dst
//! offset in the resulting plan regardless of input order.
NOLINT_TEST(UploadPlannerBasicTest, BufferPlan_SortsByDstOffset)
{
  // Create a single destination buffer
  oxygen::graphics::BufferDesc bd;
  bd.size_bytes = 1024;
  auto buf = std::make_shared<LocalDummyBuffer>(bd);

  // Two requests in reverse dst_offset order
  std::vector<UploadRequest> reqs;
  UploadRequest r1;
  r1.kind = UploadKind::kBuffer;
  r1.desc
    = UploadBufferDesc { .dst = buf, .size_bytes = 128, .dst_offset = 128 };
  UploadRequest r0;
  r0.kind = UploadKind::kBuffer;
  r0.desc = UploadBufferDesc { .dst = buf, .size_bytes = 64, .dst_offset = 0 };
  // Insert r1 then r0
  reqs.emplace_back(std::move(r1));
  reqs.emplace_back(std::move(r0));

  const auto plan = UploadPlanner::PlanBuffers(reqs, UploadPolicy {});
  ASSERT_HAS_VALUE(plan);
  const auto& p = plan.value();
  ASSERT_EQ(p.uploads.size(), 2u);

  // Verify the resulting uploads are ordered by dst_offset ascending
  EXPECT_EQ(p.uploads[0].region.dst_offset, 0u);
  EXPECT_EQ(p.uploads[1].region.dst_offset, 128u);
}

//! Texture: when subresources are provided but all are invalid/skipped,
//! the planner must return UploadError::kInvalidRequest (no regions).
NOLINT_TEST(
  UploadPlannerBasicTest, Texture2D_AllInvalidSubresources_ReturnsError)
{
  oxygen::graphics::TextureDesc td;
  td.width = 16;
  td.height = 16;
  td.depth = 1;
  td.array_size = 1;
  td.mip_levels = 1;
  td.format = oxygen::Format::kRGBA8UNorm;
  auto tex = std::make_shared<LocalDummyTexture>(td);

  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = td.depth;
  req.format = td.format;

  // Provide a subresource that's out-of-range (mip >= mip_levels)
  std::vector<UploadSubresource> subs {
    UploadSubresource { .mip = 5, .array_slice = 0 },
    // Also out-of-range array slice
    UploadSubresource { .mip = 0, .array_slice = 3 },
  };

  const auto exp_plan
    = UploadPlanner::PlanTexture2D(req, subs, UploadPolicy {});
  ASSERT_FALSE(exp_plan.has_value());
  EXPECT_EQ(exp_plan.error(), UploadError::kInvalidRequest);
}

} // namespace
