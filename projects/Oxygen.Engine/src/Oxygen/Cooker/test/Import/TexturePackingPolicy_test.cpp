//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/TexturePackingPolicy.cpp

#include <cstddef>
#include <vector>

#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Testing/GTest.h>

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
// D3D12 Packing Policy Tests
//===----------------------------------------------------------------------===//

class D3D12PackingPolicyTest : public ::testing::Test {
protected:
  const D3D12PackingPolicy& policy_ = D3D12PackingPolicy::Instance();
};

NOLINT_TEST_F(D3D12PackingPolicyTest, IdReturnsD3D12)
{
  EXPECT_EQ(policy_.Id(), "d3d12");
}

NOLINT_TEST_F(D3D12PackingPolicyTest, AlignRowPitchBytesExactMultiple)
{
  EXPECT_EQ(policy_.AlignRowPitchBytes(256), 256U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(512), 512U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(1024), 1024U);
}

NOLINT_TEST_F(D3D12PackingPolicyTest, AlignRowPitchBytesRoundsUp)
{
  EXPECT_EQ(policy_.AlignRowPitchBytes(1), 256U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(100), 256U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(255), 256U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(257), 512U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(300), 512U);
}

NOLINT_TEST_F(D3D12PackingPolicyTest, AlignSubresourceOffsetExactMultiple)
{
  EXPECT_EQ(policy_.AlignSubresourceOffset(512), 512U);
  EXPECT_EQ(policy_.AlignSubresourceOffset(1024), 1024U);
}

NOLINT_TEST_F(D3D12PackingPolicyTest, AlignSubresourceOffsetRoundsUp)
{
  EXPECT_EQ(policy_.AlignSubresourceOffset(1), 512U);
  EXPECT_EQ(policy_.AlignSubresourceOffset(511), 512U);
  EXPECT_EQ(policy_.AlignSubresourceOffset(513), 1024U);
}

//===----------------------------------------------------------------------===//
// Tight Packed Policy Tests
//===----------------------------------------------------------------------===//

class TightPackedPolicyTest : public ::testing::Test {
protected:
  const TightPackedPolicy& policy_ = TightPackedPolicy::Instance();
};

NOLINT_TEST_F(TightPackedPolicyTest, IdReturnsTight)
{
  EXPECT_EQ(policy_.Id(), "tight");
}

NOLINT_TEST_F(TightPackedPolicyTest, AlignRowPitchBytesNoPadding)
{
  EXPECT_EQ(policy_.AlignRowPitchBytes(1), 1U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(100), 100U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(256), 256U);
  EXPECT_EQ(policy_.AlignRowPitchBytes(257), 257U);
}

NOLINT_TEST_F(TightPackedPolicyTest, AlignSubresourceOffsetAligns4Bytes)
{
  EXPECT_EQ(policy_.AlignSubresourceOffset(0), 0U);
  EXPECT_EQ(policy_.AlignSubresourceOffset(1), 4U);
  EXPECT_EQ(policy_.AlignSubresourceOffset(3), 4U);
  EXPECT_EQ(policy_.AlignSubresourceOffset(4), 4U);
  EXPECT_EQ(policy_.AlignSubresourceOffset(5), 8U);
}

//===----------------------------------------------------------------------===//
// Format Utilities Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(FormatUtilitiesTest, BytesPerPixelOrBlockCommonFormats)
{
  EXPECT_EQ(ComputeBytesPerPixelOrBlock(Format::kRGBA8UNorm), 4U);
  EXPECT_EQ(ComputeBytesPerPixelOrBlock(Format::kRGBA16Float), 8U);
  EXPECT_EQ(ComputeBytesPerPixelOrBlock(Format::kRGBA32Float), 16U);
  EXPECT_EQ(ComputeBytesPerPixelOrBlock(Format::kBC7UNorm), 16U);
}

NOLINT_TEST(FormatUtilitiesTest, BlockDimensionUncompressed)
{
  EXPECT_EQ(ComputeBlockDimension(Format::kRGBA8UNorm), 1U);
  EXPECT_EQ(ComputeBlockDimension(Format::kRGBA16Float), 1U);
}

NOLINT_TEST(FormatUtilitiesTest, BlockDimensionBC7)
{
  EXPECT_EQ(ComputeBlockDimension(Format::kBC7UNorm), 4U);
}

NOLINT_TEST(FormatUtilitiesTest, RowBytesUncompressedRGBA8)
{
  EXPECT_EQ(ComputeRowBytes(64, Format::kRGBA8UNorm), 256U); // 64 * 4
  EXPECT_EQ(ComputeRowBytes(256, Format::kRGBA8UNorm), 1024U); // 256 * 4
}

NOLINT_TEST(FormatUtilitiesTest, RowBytesBC7)
{
  // BC7: 16 bytes per 4x4 block
  EXPECT_EQ(ComputeRowBytes(4, Format::kBC7UNorm), 16U); // 1 block
  EXPECT_EQ(ComputeRowBytes(8, Format::kBC7UNorm), 32U); // 2 blocks
  EXPECT_EQ(ComputeRowBytes(5, Format::kBC7UNorm), 32U); // 2 blocks (rounds up)
  EXPECT_EQ(ComputeRowBytes(256, Format::kBC7UNorm), 1024U); // 64 blocks
}

NOLINT_TEST(FormatUtilitiesTest, SurfaceBytesUncompressedRGBA8)
{
  EXPECT_EQ(
    ComputeSurfaceBytes(64, 64, Format::kRGBA8UNorm), 16384U); // 64*64*4
  EXPECT_EQ(ComputeSurfaceBytes(256, 256, Format::kRGBA8UNorm), 262144U);
}

NOLINT_TEST(FormatUtilitiesTest, SurfaceBytesBC7)
{
  // BC7: 16 bytes per 4x4 block
  EXPECT_EQ(ComputeSurfaceBytes(4, 4, Format::kBC7UNorm), 16U); // 1 block
  EXPECT_EQ(ComputeSurfaceBytes(8, 8, Format::kBC7UNorm), 64U); // 4 blocks
  EXPECT_EQ(
    ComputeSurfaceBytes(256, 256, Format::kBC7UNorm), 65536U); // 64*64 blocks
}

//===----------------------------------------------------------------------===//
// Mip Dimension Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(MipDimensionTest, ComputeMipDimensionStandardCases)
{
  EXPECT_EQ(ComputeMipDimension(256, 0), 256U);
  EXPECT_EQ(ComputeMipDimension(256, 1), 128U);
  EXPECT_EQ(ComputeMipDimension(256, 2), 64U);
  EXPECT_EQ(ComputeMipDimension(256, 3), 32U);
  EXPECT_EQ(ComputeMipDimension(256, 8), 1U);
}

NOLINT_TEST(MipDimensionTest, ComputeMipDimensionMinimumIsOne)
{
  EXPECT_EQ(ComputeMipDimension(256, 9), 1U);
  EXPECT_EQ(ComputeMipDimension(256, 10), 1U);
  EXPECT_EQ(ComputeMipDimension(1, 0), 1U);
  EXPECT_EQ(ComputeMipDimension(1, 1), 1U);
}

//===----------------------------------------------------------------------===//
// Subresource Layout Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(SubresourceLayoutTest, SingleMipRGBA8D3D12)
{
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  auto layouts
    = ComputeSubresourceLayouts(meta, D3D12PackingPolicy::Instance());

  ASSERT_EQ(layouts.size(), 1U);
  EXPECT_EQ(layouts.at(0).offset, 0U);
  EXPECT_EQ(layouts.at(0).width, 64U);
  EXPECT_EQ(layouts.at(0).height, 64U);
  EXPECT_EQ(layouts.at(0).row_pitch, 256U); // 64*4 = 256, already aligned
  EXPECT_EQ(layouts.at(0).size_bytes, 256U * 64); // row_pitch * height
}

NOLINT_TEST(SubresourceLayoutTest, MultipleMipsD3D12)
{
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 256,
    .height = 256,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 3,
    .format = Format::kRGBA8UNorm,
  };

  auto layouts
    = ComputeSubresourceLayouts(meta, D3D12PackingPolicy::Instance());

  ASSERT_EQ(layouts.size(), 3U);

  // Mip 0: 256x256
  EXPECT_EQ(layouts.at(0).width, 256U);
  EXPECT_EQ(layouts.at(0).height, 256U);
  EXPECT_EQ(layouts.at(0).row_pitch, 1024U); // 256*4, already aligned

  // Mip 1: 128x128
  EXPECT_EQ(layouts.at(1).width, 128U);
  EXPECT_EQ(layouts.at(1).height, 128U);
  EXPECT_EQ(layouts.at(1).row_pitch, 512U); // 128*4, already aligned
  // Offset should be aligned to 512
  EXPECT_EQ(layouts.at(1).offset % kD3D12SubresourcePlacementAlignment, 0U);

  // Mip 2: 64x64
  EXPECT_EQ(layouts.at(2).width, 64U);
  EXPECT_EQ(layouts.at(2).height, 64U);
  EXPECT_EQ(layouts.at(2).row_pitch, 256U); // 64*4, already aligned
  EXPECT_EQ(layouts.at(2).offset % kD3D12SubresourcePlacementAlignment, 0U);
}

NOLINT_TEST(SubresourceLayoutTest, BC7D3D12)
{
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 256,
    .height = 256,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kBC7UNorm,
  };

  auto layouts
    = ComputeSubresourceLayouts(meta, D3D12PackingPolicy::Instance());

  ASSERT_EQ(layouts.size(), 1U);
  // BC7: 256/4 = 64 blocks per row, 64 * 16 = 1024 bytes
  EXPECT_EQ(layouts.at(0).row_pitch, 1024U);
  // Size: 64 * 64 blocks * 16 bytes = 65536
  EXPECT_EQ(layouts.at(0).size_bytes, 65536U);
}

NOLINT_TEST(SubresourceLayoutTest, TightPackedNoPadding)
{
  // 65 width requires padding in D3D12
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 65,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  auto d3d12_layouts
    = ComputeSubresourceLayouts(meta, D3D12PackingPolicy::Instance());
  auto tight_layouts
    = ComputeSubresourceLayouts(meta, TightPackedPolicy::Instance());

  ASSERT_EQ(d3d12_layouts.size(), 1U);
  ASSERT_EQ(tight_layouts.size(), 1U);

  // D3D12: 65*4 = 260 -> 512 (aligned to 256)
  EXPECT_EQ(d3d12_layouts.at(0).row_pitch, 512U);

  // Tight: 65*4 = 260, no padding
  EXPECT_EQ(tight_layouts.at(0).row_pitch, 260U);

  // Tight is smaller
  EXPECT_LT(tight_layouts.at(0).size_bytes, d3d12_layouts.at(0).size_bytes);
}

NOLINT_TEST(SubresourceLayoutTest, TotalPayloadSizeMultiMip)
{
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 256,
    .height = 256,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 3,
    .format = Format::kRGBA8UNorm,
  };

  auto layouts = ComputeSubresourceLayouts(meta, TightPackedPolicy::Instance());
  auto total = ComputeTotalPayloadSize(layouts);

  // Tight packing: last offset + last size
  const auto& last = layouts.back();
  EXPECT_EQ(total, last.offset + last.size_bytes);
}

NOLINT_TEST(SubresourceLayoutTest, ArrayTextureLayoutOrder)
{
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2DArray,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 2,
    .mip_levels = 2,
    .format = Format::kRGBA8UNorm,
  };

  auto layouts = ComputeSubresourceLayouts(meta, TightPackedPolicy::Instance());

  // 2 layers * 2 mips = 4 subresources
  ASSERT_EQ(layouts.size(), 4U);

  // Order: layer 0 mip 0, layer 0 mip 1, layer 1 mip 0, layer 1 mip 1
  // Layer 0 mip 0
  EXPECT_EQ(layouts.at(0).width, 64U);
  EXPECT_EQ(layouts.at(0).height, 64U);

  // Layer 0 mip 1
  EXPECT_EQ(layouts.at(1).width, 32U);
  EXPECT_EQ(layouts.at(1).height, 32U);

  // Layer 1 mip 0
  EXPECT_EQ(layouts.at(2).width, 64U);
  EXPECT_EQ(layouts.at(2).height, 64U);

  // Layer 1 mip 1
  EXPECT_EQ(layouts.at(3).width, 32U);
  EXPECT_EQ(layouts.at(3).height, 32U);

  // All offsets should be increasing
  for (size_t i = 1; i < layouts.size(); ++i) {
    EXPECT_GT(layouts.at(i).offset, layouts.at(i - 1).offset);
  }
}

} // namespace
