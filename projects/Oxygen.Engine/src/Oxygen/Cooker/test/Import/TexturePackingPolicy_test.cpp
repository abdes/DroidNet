//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/TexturePackingPolicy.cpp

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::ComputeSubresourceLayouts;
using oxygen::content::import::ComputeTotalPayloadSize;
using oxygen::content::import::D3D12PackingPolicy;
using oxygen::content::import::kD3D12SubresourcePlacementAlignment;
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

//! A subresource of more than 4 GiB keeps its size: the cook refuses it when
//! it records the layout, rather than recording a wrapped one.
NOLINT_TEST(SubresourceLayoutTest, LargeSubresourceSizeDoesNotWrap)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 65536,
    .height = 8192,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA32Float,
  };

  const auto layouts
    = ComputeSubresourceLayouts(meta, TightPackedPolicy::Instance());

  ASSERT_EQ(layouts.size(), 1U);
  EXPECT_EQ(layouts.at(0).row_pitch, 65536U * 16U);
  EXPECT_EQ(layouts.at(0).size_bytes, uint64_t { 65536U } * 16U * 8192U);
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
