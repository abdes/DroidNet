//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

using oxygen::vortex::upload::UploadPlanner;
using oxygen::vortex::upload::UploadPolicy;
using oxygen::vortex::upload::UploadSubresource;
using oxygen::vortex::upload::UploadTextureDesc;

namespace {

class DummyTexture final : public oxygen::graphics::Texture {
public:
  explicit DummyTexture(oxygen::graphics::TextureDesc d)
    : Texture("DummyTex")
    , desc_(std::move(d))
  {
  }
  auto GetDescriptor() const -> const oxygen::graphics::TextureDesc& override
  {
    return desc_;
  }
  auto GetNativeResource() const -> oxygen::graphics::NativeResource override
  {

    return {
      this,
      ClassTypeId(),
    };
  }

protected:
  [[nodiscard]] auto CreateShaderResourceView(
    const oxygen::graphics::DescriptorAllocationHandle& /*view_handle*/,
    oxygen::Format /*format*/, oxygen::TextureType /*dimension*/,
    oxygen::graphics::TextureSubResourceSet /*sub_resources*/) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateUnorderedAccessView(
    const oxygen::graphics::DescriptorAllocationHandle& /*view_handle*/,
    oxygen::Format /*format*/, oxygen::TextureType /*dimension*/,
    oxygen::graphics::TextureSubResourceSet /*sub_resources*/) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateRenderTargetView(
    const oxygen::graphics::DescriptorAllocationHandle& /*view_handle*/,
    oxygen::Format /*format*/,
    oxygen::graphics::TextureSubResourceSet /*sub_resources*/) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateDepthStencilView(
    const oxygen::graphics::DescriptorAllocationHandle& /*view_handle*/,
    oxygen::Format /*format*/,
    oxygen::graphics::TextureSubResourceSet /*sub_resources*/,
    bool /*is_read_only*/) const -> oxygen::graphics::NativeView override
  {
    return {};
  }

private:
  oxygen::graphics::TextureDesc desc_;
};

//! Fixture for texture upload planning tests.
class UploadPlannerTextureTest : public testing::Test {
protected:
  auto SetUp() -> void override { }
  auto TearDown() -> void override { }

  [[nodiscard]] auto UploadQueueKey() const
  {
    return oxygen::graphics::QueueKey("universal");
  }
};

//! Full texture plan produces 256B-aligned row pitch and correct slice size.
NOLINT_TEST_F(UploadPlannerTextureTest, Texture2D_Full)
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
  const auto exp_plan
    = UploadPlanner::PlanTexture2D(req, {}, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  const auto& total_bytes = plan.total_bytes;
  const auto& regions = plan.regions;
  ASSERT_EQ(regions.size(), 1U);
  const auto& r = regions.at(0);
  EXPECT_EQ(r.buffer_offset, 0U);
  EXPECT_EQ(r.buffer_row_pitch, 512U); // 128 * 4 aligned to 256
  EXPECT_EQ(r.buffer_slice_pitch, 512U * 64U);
  EXPECT_EQ(total_bytes, r.buffer_slice_pitch);
}

// Two mips: aligned offsets and pitches match expectations for RGBA8.
NOLINT_TEST_F(UploadPlannerTextureTest, Texture2D_TwoMips)
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
  std::vector<oxygen::vortex::upload::UploadSubresource> subs = {
    {
      .mip = 0U,
      .array_slice = 0U,
    },
    {
      .mip = 1U,
      .array_slice = 0U,
    },
  };
  const auto exp_plan
    = UploadPlanner::PlanTexture2D(req, subs, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  ASSERT_EQ(plan.regions.size(), 2U);
  const auto& r0 = plan.regions.at(0);
  EXPECT_EQ(r0.buffer_offset, 0U);
  EXPECT_EQ(r0.buffer_row_pitch, 256U);
  EXPECT_EQ(r0.buffer_slice_pitch, 256U * 32U);
  const auto& r1 = plan.regions.at(1);
  EXPECT_EQ(r1.buffer_row_pitch, 256U);
  EXPECT_EQ(r1.buffer_slice_pitch, 256U * 16U);
  // placement-aligned offset: r0.slice_pitch is 8192; alignment 512 keeps it
  // 8192
  EXPECT_EQ(r1.buffer_offset, 8192U);
  EXPECT_EQ(plan.total_bytes, r1.buffer_offset + r1.buffer_slice_pitch);
}

// BC3 format: bytes_per_block=16, block_size=4. Validate full texture plan.
NOLINT_TEST_F(UploadPlannerTextureTest, Texture2D_BC3_Full)
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
  const auto exp_plan
    = UploadPlanner::PlanTexture2D(req, {}, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  ASSERT_EQ(plan.regions.size(), 1U);
  const auto& r = plan.regions.at(0);
  // blocks_x = 128/4 = 32 -> row = 32 * 16 = 512 (already 256 aligned)
  EXPECT_EQ(r.buffer_row_pitch, 512U);
  // blocks_y = 64/4 = 16 -> slice = 512 * 16 = 8192
  EXPECT_EQ(r.buffer_slice_pitch, 8192U);
  EXPECT_EQ(plan.total_bytes, 8192U);
}

// Partial region: plan should compute pitches based on region area, not full
// mip.
NOLINT_TEST_F(UploadPlannerTextureTest, Texture2D_PartialRegion)
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
  std::vector<oxygen::vortex::upload::UploadSubresource> subs = {
    {
      .mip = 0U,
      .array_slice = 0U,
      .x = 10U,
      .y = 5U,
      .z = 0U,
      .width = 50U,
      .height = 20U,
      .depth = 1U,
    },
  };
  const auto exp_plan
    = UploadPlanner::PlanTexture2D(req, subs, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  ASSERT_EQ(plan.regions.size(), 1U);
  const auto& r = plan.regions.at(0);
  // RGBA8: bytes_per_pixel=4, width=50 -> row=200 -> align to 256
  EXPECT_EQ(r.buffer_row_pitch, 256U);
  EXPECT_EQ(r.buffer_slice_pitch, 256U * 20U);
  EXPECT_EQ(r.dst_slice.x, 10U);
  EXPECT_EQ(r.dst_slice.y, 5U);
  EXPECT_EQ(r.dst_slice.width, 50U);
  EXPECT_EQ(r.dst_slice.height, 20U);
}

// Array slice copy: ensure distinct offsets for two slices of same mip.
NOLINT_TEST_F(UploadPlannerTextureTest, Texture2D_ArrayTwoSlices)
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
  std::vector<oxygen::vortex::upload::UploadSubresource> subs = {
    {
      .mip = 0U,
      .array_slice = 0U,
    },
    {
      .mip = 0U,
      .array_slice = 1U,
    },
  };
  const auto exp_plan
    = UploadPlanner::PlanTexture2D(req, subs, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  const auto& total_bytes = plan.total_bytes;
  const auto& regions = plan.regions;
  ASSERT_EQ(regions.size(), 2U);
  const auto& r0 = regions.at(0);
  const auto& r1 = regions.at(1);
  EXPECT_EQ(r0.buffer_row_pitch, 256U);
  EXPECT_EQ(r0.buffer_slice_pitch, 256U * 32U);
  // r1 offset should be previous slice pitch aligned to 512
  EXPECT_EQ(r1.buffer_offset, 256U * 32U);
  EXPECT_EQ(total_bytes, r1.buffer_offset + r1.buffer_slice_pitch);
}

// 3D texture: full region at mip 0 should multiply slice pitch by depth.
NOLINT_TEST_F(UploadPlannerTextureTest, Texture3D_Full)
{
  oxygen::graphics::TextureDesc td;
  td.width = 32;
  td.height = 16;
  td.depth = 8;
  td.array_size = 1;
  td.mip_levels = 1;
  td.texture_type = oxygen::TextureType::kTexture3D;
  td.format = oxygen::Format::kRGBA8UNorm;
  const auto tex = std::make_shared<DummyTexture>(td);
  UploadTextureDesc req;
  req.dst = tex;
  req.width = td.width;
  req.height = td.height;
  req.depth = td.depth;
  req.format = td.format;
  const auto exp_plan
    = UploadPlanner::PlanTexture3D(req, {}, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  const auto& total_bytes = plan.total_bytes;
  const auto& regions = plan.regions;
  ASSERT_EQ(regions.size(), 1U);
  const auto& r = regions.at(0);
  // RGBA8: row = 32*4=128 -> align 256; slice = 256*16=4096; total = 4096*8
  EXPECT_EQ(r.buffer_row_pitch, 256U);
  EXPECT_EQ(r.buffer_slice_pitch, 4096U);
  EXPECT_EQ(total_bytes, 4096U * 8U);
}

// 3D texture: partial region with z-range and smaller width/height.
NOLINT_TEST_F(UploadPlannerTextureTest, Texture3D_PartialRegion)
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
  std::vector<oxygen::vortex::upload::UploadSubresource> subs = {
    {
      .mip = 0U,
      .array_slice = 0U,
      .x = 4U,
      .y = 2U,
      .z = 3U,
      .width = 17U,
      .height = 9U,
      .depth = 5U,
    },
  };
  const auto exp_plan
    = UploadPlanner::PlanTexture3D(req, subs, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  ASSERT_EQ(plan.regions.size(), 1U);
  const auto& r = plan.regions.at(0);
  // RGBA8: row = 17*4=68 -> align 256; slice = 256*9=2304; total adds * depth
  EXPECT_EQ(r.buffer_row_pitch, 256U);
  EXPECT_EQ(r.buffer_slice_pitch, 2304U);
  EXPECT_EQ(plan.total_bytes, 2304U * 5U);
  EXPECT_EQ(r.dst_slice.x, 4U);
  EXPECT_EQ(r.dst_slice.y, 2U);
  EXPECT_EQ(r.dst_slice.z, 3U);
  EXPECT_EQ(r.dst_slice.width, 17U);
  EXPECT_EQ(r.dst_slice.height, 9U);
  EXPECT_EQ(r.dst_slice.depth, 5U);
}

// Cube treated as 2D array: plan pitches like 2D; array_slice targets face.
NOLINT_TEST_F(UploadPlannerTextureTest, TextureCube_TwoFaces)
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
  std::vector<oxygen::vortex::upload::UploadSubresource> subs = {
    {
      .mip = 0U,
      .array_slice = 0U,
    },
    {
      .mip = 0U,
      .array_slice = 3U,
    },
  };
  const auto exp_plan
    = UploadPlanner::PlanTexture2D(req, subs, UploadPolicy(UploadQueueKey()));
  ASSERT_TRUE(exp_plan.has_value());
  const auto& plan = exp_plan.value();
  const auto& total_bytes = plan.total_bytes;
  const auto& regions = plan.regions;
  ASSERT_EQ(regions.size(), 2U);
  const auto& r0 = regions.at(0);
  const auto& r1 = regions.at(1);
  EXPECT_EQ(r0.buffer_row_pitch, 256U); // 64*4 aligned 256
  EXPECT_EQ(r0.buffer_slice_pitch, 256U * 64U);
  EXPECT_EQ(r1.buffer_offset, r0.buffer_slice_pitch);
  EXPECT_EQ(total_bytes, r1.buffer_offset + r1.buffer_slice_pitch);
}

} // namespace
