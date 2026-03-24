//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Texture.h>

namespace oxygen::graphics {
namespace {

  NOLINT_TEST(TextureCopyFootprintTest, ComputesTightFootprintForUncompressed2D)
  {
    const TextureDesc desc {
      .width = 8,
      .height = 4,
      .depth = 1,
      .format = Format::kRGBA8UNorm,
      .texture_type = TextureType::kTexture2D,
    };

    const auto footprint = ComputeLinearTextureCopyFootprint(desc, {});

    EXPECT_EQ(footprint.row_count, 4U);
    EXPECT_EQ(footprint.slice_count, 1U);
    EXPECT_EQ(footprint.row_pitch, SizeBytes { 32 });
    EXPECT_EQ(footprint.slice_pitch, SizeBytes { 128 });
    EXPECT_EQ(footprint.total_bytes, SizeBytes { 128 });
  }

  NOLINT_TEST(
    TextureCopyFootprintTest, ComputesTightFootprintForBlockCompressedSlice)
  {
    const TextureDesc desc {
      .width = 7,
      .height = 5,
      .depth = 1,
      .format = Format::kBC1UNorm,
      .texture_type = TextureType::kTexture2D,
    };

    const auto footprint = ComputeLinearTextureCopyFootprint(desc, {});

    EXPECT_EQ(footprint.row_count, 2U);
    EXPECT_EQ(footprint.slice_count, 1U);
    EXPECT_EQ(footprint.row_pitch, SizeBytes { 16 });
    EXPECT_EQ(footprint.slice_pitch, SizeBytes { 32 });
    EXPECT_EQ(footprint.total_bytes, SizeBytes { 32 });
  }

  NOLINT_TEST(TextureCopyFootprintTest, ResolvesImplicitCopyRegionPitches)
  {
    const TextureDesc desc {
      .width = 9,
      .height = 4,
      .depth = 3,
      .format = Format::kRGBA8UNorm,
      .texture_type = TextureType::kTexture3D,
    };

    const TextureBufferCopyRegion region {
      .buffer_offset = OffsetBytes { 64 },
      .buffer_row_pitch = SizeBytes { 0 },
      .buffer_slice_pitch = SizeBytes { 0 },
      .texture_slice = { .width = 5, .height = 3, .depth = 2 },
    };

    const auto resolved = ResolveTextureBufferCopyRegion(desc, region);

    EXPECT_EQ(resolved.buffer_offset, OffsetBytes { 64 });
    EXPECT_EQ(resolved.texture_slice.width, 5U);
    EXPECT_EQ(resolved.texture_slice.height, 3U);
    EXPECT_EQ(resolved.texture_slice.depth, 2U);
    EXPECT_EQ(resolved.buffer_row_pitch, SizeBytes { 20 });
    EXPECT_EQ(resolved.buffer_slice_pitch, SizeBytes { 60 });
  }

  NOLINT_TEST(TextureCopyFootprintTest, AppliesExplicitRowPitchAlignment)
  {
    const auto footprint
      = ComputeLinearTextureCopyFootprint(Format::kRGBA8UNorm,
        LinearTextureExtent {
          .width = 50,
          .height = 20,
          .depth = 1,
        },
        SizeBytes { 256 });

    EXPECT_EQ(footprint.row_count, 20U);
    EXPECT_EQ(footprint.slice_count, 1U);
    EXPECT_EQ(footprint.row_pitch, SizeBytes { 256 });
    EXPECT_EQ(footprint.slice_pitch, SizeBytes { 5120 });
    EXPECT_EQ(footprint.total_bytes, SizeBytes { 5120 });
  }

  NOLINT_TEST(
    TextureCopyFootprintTest, PreservesExplicitRowPitchWhenResolvingSlicePitch)
  {
    const TextureDesc desc {
      .width = 8,
      .height = 4,
      .depth = 2,
      .format = Format::kRGBA8UNorm,
      .texture_type = TextureType::kTexture3D,
    };

    const TextureBufferCopyRegion region {
      .buffer_offset = OffsetBytes { 0 },
      .buffer_row_pitch = SizeBytes { 64 },
      .buffer_slice_pitch = SizeBytes { 0 },
      .texture_slice = { .width = 8, .height = 4, .depth = 2 },
    };

    const auto resolved = ResolveTextureBufferCopyRegion(desc, region);

    EXPECT_EQ(resolved.buffer_row_pitch, SizeBytes { 64 });
    EXPECT_EQ(resolved.buffer_slice_pitch, SizeBytes { 256 });
  }

} // namespace
} // namespace oxygen::graphics
