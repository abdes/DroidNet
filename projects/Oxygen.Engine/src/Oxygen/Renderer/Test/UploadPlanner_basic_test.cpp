//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Upload/UploadPlanner.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

#include <memory>
#include <vector>

using oxygen::engine::upload::UploadPlanner;
using oxygen::engine::upload::UploadPolicy;
using oxygen::engine::upload::UploadSubresource;
using oxygen::engine::upload::UploadTextureDesc;

namespace {
class DummyTexture : public oxygen::graphics::Texture {
public:
  explicit DummyTexture(const oxygen::graphics::TextureDesc& d)
    : Texture("DummyTex")
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
      const_cast<DummyTexture*>(this), ClassTypeId());
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
} // namespace

//! Full texture plan produces 256B-aligned row pitch and correct slice size.
TEST(UploadPlanner, Texture2D_Full)
{
  oxygen::graphics::TextureDesc td;
  td.width = 128;
  td.height = 64;
  td.depth = 1;
  td.array_size = 1;
  td.mip_levels = 1;
  td.format = oxygen::Format::kRGBA8UNorm;
  auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = 1;
  req.format = td.format;
  const auto plan = UploadPlanner::PlanTexture2D(req, {}, UploadPolicy {});
  ASSERT_EQ(plan.regions.size(), 1u);
  const auto& r = plan.regions[0];
  EXPECT_EQ(r.buffer_offset, 0u);
  EXPECT_EQ(r.buffer_row_pitch, 512u); // 128 * 4 aligned to 256
  EXPECT_EQ(r.buffer_slice_pitch, 512u * 64u);
  EXPECT_EQ(plan.total_bytes, r.buffer_slice_pitch);
}

// Two mips: aligned offsets and pitches match expectations for RGBA8.
TEST(UploadPlanner, Texture2D_TwoMips)
{
  oxygen::graphics::TextureDesc td;
  td.width = 64;
  td.height = 32;
  td.depth = 1;
  td.array_size = 1;
  td.mip_levels = 2;
  td.format = oxygen::Format::kRGBA8UNorm;
  auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = 1;
  req.format = td.format;
  std::vector<UploadSubresource> subs {
    UploadSubresource { .mip = 0, .array_slice = 0 },
    UploadSubresource { .mip = 1, .array_slice = 0 },
  };
  const auto plan = UploadPlanner::PlanTexture2D(req, subs, UploadPolicy {});
  ASSERT_EQ(plan.regions.size(), 2u);
  const auto& r0 = plan.regions[0];
  EXPECT_EQ(r0.buffer_offset, 0u);
  EXPECT_EQ(r0.buffer_row_pitch, 256u);
  EXPECT_EQ(r0.buffer_slice_pitch, 256u * 32u);
  const auto& r1 = plan.regions[1];
  EXPECT_EQ(r1.buffer_row_pitch, 256u);
  EXPECT_EQ(r1.buffer_slice_pitch, 256u * 16u);
  // placement-aligned offset: r0.slice_pitch is 8192; alignment 512 keeps it
  // 8192
  EXPECT_EQ(r1.buffer_offset, 8192u);
  EXPECT_EQ(plan.total_bytes, r1.buffer_offset + r1.buffer_slice_pitch);
}

// BC3 format: bytes_per_block=16, block_size=4. Validate full texture plan.
TEST(UploadPlanner, Texture2D_BC3_Full)
{
  oxygen::graphics::TextureDesc td;
  td.width = 128; // divisible by 4
  td.height = 64; // divisible by 4
  td.depth = 1;
  td.array_size = 1;
  td.mip_levels = 1;
  td.format = oxygen::Format::kBC3UNorm;
  auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = 1;
  req.format = td.format;
  const auto plan = UploadPlanner::PlanTexture2D(req, {}, UploadPolicy {});
  ASSERT_EQ(plan.regions.size(), 1u);
  const auto& r = plan.regions[0];
  // blocks_x = 128/4 = 32 -> row = 32 * 16 = 512 (already 256 aligned)
  EXPECT_EQ(r.buffer_row_pitch, 512u);
  // blocks_y = 64/4 = 16 -> slice = 512 * 16 = 8192
  EXPECT_EQ(r.buffer_slice_pitch, 8192u);
  EXPECT_EQ(plan.total_bytes, 8192u);
}

// Partial region: plan should compute pitches based on region area, not full
// mip.
TEST(UploadPlanner, Texture2D_PartialRegion)
{
  oxygen::graphics::TextureDesc td;
  td.width = 100; // non-multiple to exercise ceil block math
  td.height = 60;
  td.depth = 1;
  td.array_size = 1;
  td.mip_levels = 1;
  td.format = oxygen::Format::kRGBA8UNorm;
  auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = 1;
  req.format = td.format;
  std::vector<UploadSubresource> subs {
    UploadSubresource { .mip = 0,
      .array_slice = 0,
      .x = 10,
      .y = 5,
      .z = 0,
      .width = 50,
      .height = 20,
      .depth = 1 },
  };
  const auto plan = UploadPlanner::PlanTexture2D(req, subs, UploadPolicy {});
  ASSERT_EQ(plan.regions.size(), 1u);
  const auto& r = plan.regions[0];
  // RGBA8: bytes_per_pixel=4, width=50 -> row=200 -> align to 256
  EXPECT_EQ(r.buffer_row_pitch, 256u);
  EXPECT_EQ(r.buffer_slice_pitch, 256u * 20u);
  EXPECT_EQ(r.dst_slice.x, 10u);
  EXPECT_EQ(r.dst_slice.y, 5u);
  EXPECT_EQ(r.dst_slice.width, 50u);
  EXPECT_EQ(r.dst_slice.height, 20u);
}

// Array slice copy: ensure distinct offsets for two slices of same mip.
TEST(UploadPlanner, Texture2D_ArrayTwoSlices)
{
  oxygen::graphics::TextureDesc td;
  td.width = 64;
  td.height = 32;
  td.depth = 1;
  td.array_size = 2;
  td.mip_levels = 1;
  td.format = oxygen::Format::kRGBA8UNorm;
  auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = 1;
  req.format = td.format;
  std::vector<UploadSubresource> subs {
    UploadSubresource { .mip = 0, .array_slice = 0 },
    UploadSubresource { .mip = 0, .array_slice = 1 },
  };
  const auto plan = UploadPlanner::PlanTexture2D(req, subs, UploadPolicy {});
  ASSERT_EQ(plan.regions.size(), 2u);
  const auto& r0 = plan.regions[0];
  const auto& r1 = plan.regions[1];
  EXPECT_EQ(r0.buffer_row_pitch, 256u);
  EXPECT_EQ(r0.buffer_slice_pitch, 256u * 32u);
  // r1 offset should be previous slice pitch aligned to 512
  EXPECT_EQ(r1.buffer_offset, 256u * 32u);
  EXPECT_EQ(plan.total_bytes, r1.buffer_offset + r1.buffer_slice_pitch);
}

// 3D texture: full region at mip 0 should multiply slice pitch by depth.
TEST(UploadPlanner, Texture3D_Full)
{
  oxygen::graphics::TextureDesc td;
  td.width = 32;
  td.height = 16;
  td.depth = 8;
  td.array_size = 1;
  td.mip_levels = 1;
  td.texture_type = oxygen::TextureType::kTexture3D;
  td.format = oxygen::Format::kRGBA8UNorm;
  auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = td.depth;
  req.format = td.format;
  const auto plan = UploadPlanner::PlanTexture3D(req, {}, UploadPolicy {});
  ASSERT_EQ(plan.regions.size(), 1u);
  const auto& r = plan.regions[0];
  // RGBA8: row = 32*4=128 -> align 256; slice = 256*16=4096; total = 4096*8
  EXPECT_EQ(r.buffer_row_pitch, 256u);
  EXPECT_EQ(r.buffer_slice_pitch, 4096u);
  EXPECT_EQ(plan.total_bytes, 4096u * 8u);
}

// 3D texture: partial region with z-range and smaller width/height.
TEST(UploadPlanner, Texture3D_PartialRegion)
{
  oxygen::graphics::TextureDesc td;
  td.width = 40;
  td.height = 20;
  td.depth = 16;
  td.array_size = 1;
  td.mip_levels = 1;
  td.texture_type = oxygen::TextureType::kTexture3D;
  td.format = oxygen::Format::kRGBA8UNorm;
  auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = td.depth;
  req.format = td.format;
  std::vector<UploadSubresource> subs {
    UploadSubresource { .mip = 0,
      .array_slice = 0,
      .x = 4,
      .y = 2,
      .z = 3,
      .width = 17,
      .height = 9,
      .depth = 5 },
  };
  const auto plan = UploadPlanner::PlanTexture3D(req, subs, UploadPolicy {});
  ASSERT_EQ(plan.regions.size(), 1u);
  const auto& r = plan.regions[0];
  // RGBA8: row = 17*4=68 -> align 256; slice = 256*9=2304; total adds * depth
  EXPECT_EQ(r.buffer_row_pitch, 256u);
  EXPECT_EQ(r.buffer_slice_pitch, 2304u);
  EXPECT_EQ(plan.total_bytes, 2304u * 5u);
  EXPECT_EQ(r.dst_slice.x, 4u);
  EXPECT_EQ(r.dst_slice.y, 2u);
  EXPECT_EQ(r.dst_slice.z, 3u);
  EXPECT_EQ(r.dst_slice.width, 17u);
  EXPECT_EQ(r.dst_slice.height, 9u);
  EXPECT_EQ(r.dst_slice.depth, 5u);
}

// Cube treated as 2D array: plan pitches like 2D; array_slice targets face.
TEST(UploadPlanner, TextureCube_TwoFaces)
{
  oxygen::graphics::TextureDesc td;
  td.width = 64;
  td.height = 64;
  td.depth = 1;
  td.array_size = 6; // 6 faces
  td.mip_levels = 1;
  td.texture_type = oxygen::TextureType::kTextureCube;
  td.format = oxygen::Format::kRGBA8UNorm;
  auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = 1;
  req.format = td.format;
  std::vector<UploadSubresource> subs {
    UploadSubresource { .mip = 0, .array_slice = 0 },
    UploadSubresource { .mip = 0, .array_slice = 3 },
  };
  const auto plan = UploadPlanner::PlanTextureCube(req, subs, UploadPolicy {});
  ASSERT_EQ(plan.regions.size(), 2u);
  const auto& r0 = plan.regions[0];
  const auto& r1 = plan.regions[1];
  EXPECT_EQ(r0.buffer_row_pitch, 256u); // 64*4 aligned 256
  EXPECT_EQ(r0.buffer_slice_pitch, 256u * 64u);
  EXPECT_EQ(r1.buffer_offset, r0.buffer_slice_pitch);
  EXPECT_EQ(plan.total_bytes, r1.buffer_offset + r1.buffer_slice_pitch);
}
