//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::ComputeBlockDimension;
using oxygen::content::import::ComputeBytesPerPixelOrBlock;
using oxygen::content::import::ComputeMipDimension;
using oxygen::content::import::ComputeRowBytes;
using oxygen::content::import::ComputeSubresourceLayouts;
using oxygen::content::import::ComputeSurfaceBytes;
using oxygen::content::import::ComputeTotalPayloadSize;
using oxygen::content::import::D3D12PackingPolicy;
using oxygen::content::import::kD3D12RowPitchAlignment;
using oxygen::content::import::kD3D12SubresourcePlacementAlignment;
using oxygen::content::import::kTightPackedSubresourceAlignment;
using oxygen::content::import::ScratchImageMeta;
using oxygen::content::import::TightPackedPolicy;

//===----------------------------------------------------------------------===//
// D3D12 Packing Policy Tests (5.2)
//===----------------------------------------------------------------------===//

class D3D12PackingPolicyTest : public ::testing::Test {
protected:
  const D3D12PackingPolicy& policy_ = D3D12PackingPolicy::Instance();
};

//! Test: D3D12 policy has correct ID.
NOLINT_TEST_F(D3D12PackingPolicyTest, Id_ReturnsD3D12)
{
  // Arrange & Act & Assert
  EXPECT_EQ(policy_.Id(), "d3d12");
}

//! Test: D3D12 row pitch alignment handles exact multiples.
NOLINT_TEST_F(D3D12PackingPolicyTest, AlignRowPitchBytes_ExactMultiple)
{
  // Arrange & Act & Assert
  EXPECT_EQ(policy_.AlignRowPitchBytes(256), 256u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(512), 512u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(1024), 1024u);
}

//! Test: D3D12 row pitch alignment rounds up.
NOLINT_TEST_F(D3D12PackingPolicyTest, AlignRowPitchBytes_RoundsUp)
{
  // Arrange & Act & Assert
  EXPECT_EQ(policy_.AlignRowPitchBytes(1), 256u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(100), 256u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(255), 256u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(257), 512u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(300), 512u);
}

//! Test: D3D12 subresource offset alignment handles exact multiples.
NOLINT_TEST_F(D3D12PackingPolicyTest, AlignSubresourceOffset_ExactMultiple)
{
  // Arrange & Act & Assert
  EXPECT_EQ(policy_.AlignSubresourceOffset(512), 512u);
  EXPECT_EQ(policy_.AlignSubresourceOffset(1024), 1024u);
}

//! Test: D3D12 subresource offset alignment rounds up.
NOLINT_TEST_F(D3D12PackingPolicyTest, AlignSubresourceOffset_RoundsUp)
{
  // Arrange & Act & Assert
  EXPECT_EQ(policy_.AlignSubresourceOffset(1), 512u);
  EXPECT_EQ(policy_.AlignSubresourceOffset(511), 512u);
  EXPECT_EQ(policy_.AlignSubresourceOffset(513), 1024u);
}

//===----------------------------------------------------------------------===//
// Tight Packed Policy Tests (5.3)
//===----------------------------------------------------------------------===//

class TightPackedPolicyTest : public ::testing::Test {
protected:
  const TightPackedPolicy& policy_ = TightPackedPolicy::Instance();
};

//! Test: Tight policy has correct ID.
NOLINT_TEST_F(TightPackedPolicyTest, Id_ReturnsTight)
{
  // Arrange & Act & Assert
  EXPECT_EQ(policy_.Id(), "tight");
}

//! Test: Tight policy does not pad row pitch.
NOLINT_TEST_F(TightPackedPolicyTest, AlignRowPitchBytes_NoPadding)
{
  // Arrange & Act & Assert
  EXPECT_EQ(policy_.AlignRowPitchBytes(1), 1u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(100), 100u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(256), 256u);
  EXPECT_EQ(policy_.AlignRowPitchBytes(257), 257u);
}

//! Test: Tight policy aligns subresource offset to 4 bytes.
NOLINT_TEST_F(TightPackedPolicyTest, AlignSubresourceOffset_Aligns4Bytes)
{
  // Arrange & Act & Assert
  EXPECT_EQ(policy_.AlignSubresourceOffset(0), 0u);
  EXPECT_EQ(policy_.AlignSubresourceOffset(1), 4u);
  EXPECT_EQ(policy_.AlignSubresourceOffset(3), 4u);
  EXPECT_EQ(policy_.AlignSubresourceOffset(4), 4u);
  EXPECT_EQ(policy_.AlignSubresourceOffset(5), 8u);
}

//===----------------------------------------------------------------------===//
// Format Utilities Tests (5.4)
//===----------------------------------------------------------------------===//

class FormatUtilitiesTest : public ::testing::Test { };

//! Test: ComputeBytesPerPixelOrBlock returns correct values for common formats.
NOLINT_TEST_F(FormatUtilitiesTest, BytesPerPixelOrBlock_CommonFormats)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ComputeBytesPerPixelOrBlock(Format::kRGBA8UNorm), 4u);
  EXPECT_EQ(ComputeBytesPerPixelOrBlock(Format::kRGBA16Float), 8u);
  EXPECT_EQ(ComputeBytesPerPixelOrBlock(Format::kRGBA32Float), 16u);
  EXPECT_EQ(ComputeBytesPerPixelOrBlock(Format::kBC7UNorm), 16u);
}

//! Test: ComputeBlockDimension returns 1 for uncompressed formats.
NOLINT_TEST_F(FormatUtilitiesTest, BlockDimension_Uncompressed)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ComputeBlockDimension(Format::kRGBA8UNorm), 1u);
  EXPECT_EQ(ComputeBlockDimension(Format::kRGBA16Float), 1u);
}

//! Test: ComputeBlockDimension returns 4 for BC formats.
NOLINT_TEST_F(FormatUtilitiesTest, BlockDimension_BC7)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ComputeBlockDimension(Format::kBC7UNorm), 4u);
}

//! Test: ComputeRowBytes for uncompressed format.
NOLINT_TEST_F(FormatUtilitiesTest, RowBytes_UncompressedRGBA8)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ComputeRowBytes(64, Format::kRGBA8UNorm), 256u); // 64 * 4
  EXPECT_EQ(ComputeRowBytes(256, Format::kRGBA8UNorm), 1024u); // 256 * 4
}

//! Test: ComputeRowBytes for BC7 format.
NOLINT_TEST_F(FormatUtilitiesTest, RowBytes_BC7)
{
  // Arrange & Act & Assert
  // BC7: 16 bytes per 4x4 block
  EXPECT_EQ(ComputeRowBytes(4, Format::kBC7UNorm), 16u); // 1 block
  EXPECT_EQ(ComputeRowBytes(8, Format::kBC7UNorm), 32u); // 2 blocks
  EXPECT_EQ(ComputeRowBytes(5, Format::kBC7UNorm), 32u); // 2 blocks (rounds up)
  EXPECT_EQ(ComputeRowBytes(256, Format::kBC7UNorm), 1024u); // 64 blocks
}

//! Test: ComputeSurfaceBytes for uncompressed format.
NOLINT_TEST_F(FormatUtilitiesTest, SurfaceBytes_UncompressedRGBA8)
{
  // Arrange & Act & Assert
  EXPECT_EQ(
    ComputeSurfaceBytes(64, 64, Format::kRGBA8UNorm), 16384u); // 64*64*4
  EXPECT_EQ(ComputeSurfaceBytes(256, 256, Format::kRGBA8UNorm), 262144u);
}

//! Test: ComputeSurfaceBytes for BC7 format.
NOLINT_TEST_F(FormatUtilitiesTest, SurfaceBytes_BC7)
{
  // Arrange & Act & Assert
  // BC7: 16 bytes per 4x4 block
  EXPECT_EQ(ComputeSurfaceBytes(4, 4, Format::kBC7UNorm), 16u); // 1 block
  EXPECT_EQ(ComputeSurfaceBytes(8, 8, Format::kBC7UNorm), 64u); // 4 blocks
  EXPECT_EQ(
    ComputeSurfaceBytes(256, 256, Format::kBC7UNorm), 65536u); // 64*64 blocks
}

//===----------------------------------------------------------------------===//
// Mip Dimension Tests
//===----------------------------------------------------------------------===//

class MipDimensionTest : public ::testing::Test { };

//! Test: ComputeMipDimension computes correct values.
NOLINT_TEST_F(MipDimensionTest, ComputeMipDimension_StandardCases)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ComputeMipDimension(256, 0), 256u);
  EXPECT_EQ(ComputeMipDimension(256, 1), 128u);
  EXPECT_EQ(ComputeMipDimension(256, 2), 64u);
  EXPECT_EQ(ComputeMipDimension(256, 3), 32u);
  EXPECT_EQ(ComputeMipDimension(256, 8), 1u);
}

//! Test: ComputeMipDimension returns minimum of 1.
NOLINT_TEST_F(MipDimensionTest, ComputeMipDimension_MinimumIsOne)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ComputeMipDimension(256, 9), 1u);
  EXPECT_EQ(ComputeMipDimension(256, 10), 1u);
  EXPECT_EQ(ComputeMipDimension(1, 0), 1u);
  EXPECT_EQ(ComputeMipDimension(1, 1), 1u);
}

//===----------------------------------------------------------------------===//
// Subresource Layout Tests (5.4)
//===----------------------------------------------------------------------===//

class SubresourceLayoutTest : public ::testing::Test { };

//! Test: Single mip RGBA8 texture layout with D3D12 policy.
NOLINT_TEST_F(SubresourceLayoutTest, SingleMip_RGBA8_D3D12)
{
  // Arrange
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto layouts
    = ComputeSubresourceLayouts(meta, D3D12PackingPolicy::Instance());

  // Assert
  ASSERT_EQ(layouts.size(), 1u);
  EXPECT_EQ(layouts[0].offset, 0u);
  EXPECT_EQ(layouts[0].width, 64u);
  EXPECT_EQ(layouts[0].height, 64u);
  EXPECT_EQ(layouts[0].row_pitch, 256u); // 64*4 = 256, already aligned
  EXPECT_EQ(layouts[0].size_bytes, 256u * 64); // row_pitch * height
}

//! Test: Multiple mips layout with D3D12 policy.
NOLINT_TEST_F(SubresourceLayoutTest, MultipleMips_D3D12)
{
  // Arrange
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 256,
    .height = 256,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 3,
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto layouts
    = ComputeSubresourceLayouts(meta, D3D12PackingPolicy::Instance());

  // Assert
  ASSERT_EQ(layouts.size(), 3u);

  // Mip 0: 256x256
  EXPECT_EQ(layouts[0].width, 256u);
  EXPECT_EQ(layouts[0].height, 256u);
  EXPECT_EQ(layouts[0].row_pitch, 1024u); // 256*4, already aligned

  // Mip 1: 128x128
  EXPECT_EQ(layouts[1].width, 128u);
  EXPECT_EQ(layouts[1].height, 128u);
  EXPECT_EQ(layouts[1].row_pitch, 512u); // 128*4, already aligned
  // Offset should be aligned to 512
  EXPECT_EQ(layouts[1].offset % kD3D12SubresourcePlacementAlignment, 0u);

  // Mip 2: 64x64
  EXPECT_EQ(layouts[2].width, 64u);
  EXPECT_EQ(layouts[2].height, 64u);
  EXPECT_EQ(layouts[2].row_pitch, 256u); // 64*4, already aligned
  EXPECT_EQ(layouts[2].offset % kD3D12SubresourcePlacementAlignment, 0u);
}

//! Test: BC7 texture layout with D3D12 policy.
NOLINT_TEST_F(SubresourceLayoutTest, BC7_D3D12)
{
  // Arrange
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 256,
    .height = 256,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kBC7UNorm,
  };

  // Act
  auto layouts
    = ComputeSubresourceLayouts(meta, D3D12PackingPolicy::Instance());

  // Assert
  ASSERT_EQ(layouts.size(), 1u);
  // BC7: 256/4 = 64 blocks per row, 64 * 16 = 1024 bytes
  EXPECT_EQ(layouts[0].row_pitch, 1024u);
  // Size: 64 * 64 blocks * 16 bytes = 65536
  EXPECT_EQ(layouts[0].size_bytes, 65536u);
}

//! Test: Tight packing produces smaller layout.
NOLINT_TEST_F(SubresourceLayoutTest, TightPacked_NoPadding)
{
  // Arrange - 65 width requires padding in D3D12
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 65,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto d3d12_layouts
    = ComputeSubresourceLayouts(meta, D3D12PackingPolicy::Instance());
  auto tight_layouts
    = ComputeSubresourceLayouts(meta, TightPackedPolicy::Instance());

  // Assert
  ASSERT_EQ(d3d12_layouts.size(), 1u);
  ASSERT_EQ(tight_layouts.size(), 1u);

  // D3D12: 65*4 = 260 -> 512 (aligned to 256)
  EXPECT_EQ(d3d12_layouts[0].row_pitch, 512u);

  // Tight: 65*4 = 260, no padding
  EXPECT_EQ(tight_layouts[0].row_pitch, 260u);

  // Tight is smaller
  EXPECT_LT(tight_layouts[0].size_bytes, d3d12_layouts[0].size_bytes);
}

//! Test: ComputeTotalPayloadSize sums correctly.
NOLINT_TEST_F(SubresourceLayoutTest, TotalPayloadSize_MultiMip)
{
  // Arrange
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 256,
    .height = 256,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 3,
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto layouts = ComputeSubresourceLayouts(meta, TightPackedPolicy::Instance());
  auto total = ComputeTotalPayloadSize(layouts);

  // Assert - tight packing: last offset + last size
  const auto& last = layouts.back();
  EXPECT_EQ(total, last.offset + last.size_bytes);
}

//! Test: Array texture layout.
NOLINT_TEST_F(SubresourceLayoutTest, ArrayTexture_LayoutOrder)
{
  // Arrange
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2DArray,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 2,
    .mip_levels = 2,
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto layouts = ComputeSubresourceLayouts(meta, TightPackedPolicy::Instance());

  // Assert - 2 layers * 2 mips = 4 subresources
  ASSERT_EQ(layouts.size(), 4u);

  // Order: layer 0 mip 0, layer 0 mip 1, layer 1 mip 0, layer 1 mip 1
  // Layer 0 mip 0
  EXPECT_EQ(layouts[0].width, 64u);
  EXPECT_EQ(layouts[0].height, 64u);

  // Layer 0 mip 1
  EXPECT_EQ(layouts[1].width, 32u);
  EXPECT_EQ(layouts[1].height, 32u);

  // Layer 1 mip 0
  EXPECT_EQ(layouts[2].width, 64u);
  EXPECT_EQ(layouts[2].height, 64u);

  // Layer 1 mip 1
  EXPECT_EQ(layouts[3].width, 32u);
  EXPECT_EQ(layouts[3].height, 32u);

  // All offsets should be increasing
  for (size_t i = 1; i < layouts.size(); ++i) {
    EXPECT_GT(layouts[i].offset, layouts[i - 1].offset);
  }
}

} // namespace
