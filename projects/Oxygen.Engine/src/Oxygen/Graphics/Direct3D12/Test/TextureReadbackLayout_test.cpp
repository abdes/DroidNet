//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Direct3D12/Detail/TextureReadback.h>
#include <Oxygen/Graphics/Direct3D12/Test/Mocks/MockDevice.h>

namespace {

using oxygen::graphics::TextureBufferCopyRegion;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::d3d12::detail::ComputeReadbackSurfaceLayout;
using oxygen::graphics::d3d12::detail::ComputeTextureToBufferCopyInfo;
using oxygen::graphics::d3d12::detail::MakeTextureResourceDesc;
using oxygen::graphics::d3d12::testing::MockDevice;

class FootprintDevice final : public MockDevice {
public:
  void STDMETHODCALLTYPE GetCopyableFootprints(
    const D3D12_RESOURCE_DESC* /*desc*/, UINT first_subresource,
    UINT num_subresources, UINT64 base_offset,
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT* layouts, UINT* row_counts,
    UINT64* row_sizes, UINT64* total_bytes) override
  {
    for (UINT index = 0; index < num_subresources; ++index) {
      layouts[index].Offset = base_offset + static_cast<UINT64>(index) * 512;
      layouts[index].Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      layouts[index].Footprint.Width = 8;
      layouts[index].Footprint.Height = 4;
      layouts[index].Footprint.Depth = 1;
      layouts[index].Footprint.RowPitch = 256;
      row_counts[index] = 4;
      row_sizes[index] = 32;
    }
    if (total_bytes != nullptr) {
      *total_bytes = base_offset + static_cast<UINT64>(num_subresources) * 512;
    }
    observed_first_subresource_ = first_subresource;
    observed_num_subresources_ = num_subresources;
  }

  UINT observed_first_subresource_ = 0;
  UINT observed_num_subresources_ = 0;
};

NOLINT_TEST(TextureReadbackLayoutTest,
  CopyInfo_DefaultsToD3D12AlignedRowPitchAndSlicePitch)
{
  TextureDesc desc {};
  desc.width = 5;
  desc.height = 3;
  desc.format = oxygen::Format::kRGBA8UNorm;

  const auto info = ComputeTextureToBufferCopyInfo(desc,
    DXGI_FORMAT_R8G8B8A8_UNORM, 1,
    TextureBufferCopyRegion {
      .texture_slice = TextureSlice {
        .x = 0,
        .y = 0,
        .z = 0,
        .width = 5,
        .height = 3,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
      },
    });

  EXPECT_EQ(info.placed_footprint.Footprint.RowPitch, 256u);
  EXPECT_EQ(info.resolved_region.buffer_row_pitch.get(), 256u);
  EXPECT_EQ(info.resolved_region.buffer_slice_pitch.get(), 256u * 3u);
  EXPECT_EQ(info.source_box.right, 5u);
  EXPECT_EQ(info.source_box.bottom, 3u);
  EXPECT_EQ(info.subresource_index, 0u);
}

NOLINT_TEST(TextureReadbackLayoutTest,
  CopyInfo_PreservesExplicitPitchesForBlockCompressedRegion)
{
  TextureDesc desc {};
  desc.width = 8;
  desc.height = 8;
  desc.format = oxygen::Format::kBC1UNorm;

  const auto info = ComputeTextureToBufferCopyInfo(desc, DXGI_FORMAT_BC1_UNORM,
    1,
    TextureBufferCopyRegion {
      .buffer_offset = oxygen::OffsetBytes { 512 },
      .buffer_row_pitch = oxygen::SizeBytes { 256 },
      .buffer_slice_pitch = oxygen::SizeBytes { 1024 },
      .texture_slice = TextureSlice {
        .x = 4,
        .y = 0,
        .z = 0,
        .width = 4,
        .height = 8,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
      },
    });

  EXPECT_EQ(info.placed_footprint.Offset, 512u);
  EXPECT_EQ(info.placed_footprint.Footprint.RowPitch, 256u);
  EXPECT_EQ(info.resolved_region.buffer_slice_pitch.get(), 1024u);
  EXPECT_EQ(info.source_box.left, 4u);
  EXPECT_EQ(info.source_box.right, 8u);
}

NOLINT_TEST(
  TextureReadbackLayoutTest, ReadbackSurfaceLayout_UsesDeviceReportedFootprints)
{
  FootprintDevice device;
  TextureDesc desc {};
  desc.width = 8;
  desc.height = 8;
  desc.array_size = 2;
  desc.mip_levels = 3;
  desc.format = oxygen::Format::kRGBA8UNorm;

  const auto layout
    = ComputeReadbackSurfaceLayout(device, MakeTextureResourceDesc(desc), 6, 1);

  EXPECT_EQ(device.observed_first_subresource_, 0u);
  EXPECT_EQ(device.observed_num_subresources_, 6u);
  ASSERT_EQ(layout.subresources.size(), 6u);
  EXPECT_EQ(layout.subresource_count, 6u);
  EXPECT_EQ(layout.total_bytes, 6u * 512u);
  EXPECT_EQ(layout.subresources[1].placed_footprint.Offset, 512u);
  EXPECT_EQ(layout.subresources[1].row_count, 4u);
  EXPECT_EQ(layout.subresources[1].row_size_bytes, 32u);
}

NOLINT_TEST(TextureReadbackLayoutTest,
  MakeTextureResourceDesc_DoesNotDenyShaderResourceForColorTextures)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;

  const auto resource_desc = MakeTextureResourceDesc(desc);

  EXPECT_EQ(resource_desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
    D3D12_RESOURCE_FLAG_NONE);
}

NOLINT_TEST(TextureReadbackLayoutTest,
  MakeTextureResourceDesc_DeniesShaderResourceForDepthStencilTextures)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kDepth32;
  desc.is_render_target = true;

  const auto resource_desc = MakeTextureResourceDesc(desc);

  EXPECT_EQ(resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
  EXPECT_EQ(resource_desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
    D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
}

} // namespace
