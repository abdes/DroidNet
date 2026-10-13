//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ScratchImage.cpp

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::ImageView;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::ScratchImageMeta;

//=== ScratchImage Basic Tests ===-------------------------------------------//

//! Test fixture for basic ScratchImage tests.
//! Default-constructed ScratchImage should be invalid.
NOLINT_TEST(ScratchImageBasicTest, DefaultConstructionCreatesInvalidImage)
{
  const ScratchImage image;

  EXPECT_FALSE(image.IsValid());
  EXPECT_EQ(image.GetTotalSizeBytes(), 0U);
  EXPECT_EQ(image.GetSubresourceCount(), 0U);
}

//! ComputeMipCount returns correct values for various dimensions.
NOLINT_TEST(ScratchImageBasicTest, ComputeMipCountReturnsCorrectValues)
{
  EXPECT_EQ(ScratchImage::ComputeMipCount(1, 1), 1U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(2, 2), 2U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(4, 4), 3U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(8, 8), 4U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(16, 16), 5U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(256, 256), 9U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(1024, 1024), 11U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(2048, 2048), 12U);
}

//! ComputeMipCount handles non-square textures correctly.
NOLINT_TEST(ScratchImageBasicTest, ComputeMipCountNonSquareTextures)
{
  EXPECT_EQ(ScratchImage::ComputeMipCount(1024, 512), 11U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(512, 1024), 11U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(4, 1), 3U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(1, 4), 3U);
}

//! ComputeMipCount returns 0 for zero dimensions.
NOLINT_TEST(ScratchImageBasicTest, ComputeMipCountZeroDimensions)
{
  EXPECT_EQ(ScratchImage::ComputeMipCount(0, 0), 0U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(0, 100), 0U);
  EXPECT_EQ(ScratchImage::ComputeMipCount(100, 0), 0U);
}

//! ComputeSubresourceIndex follows layer-major ordering.
NOLINT_TEST(ScratchImageBasicTest, ComputeSubresourceIndexLayerMajorOrdering)
{
  constexpr uint16_t kMipLevels = 4;

  // Layer 0: mips 0-3
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(0, 0, kMipLevels), 0U);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(0, 1, kMipLevels), 1U);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(0, 2, kMipLevels), 2U);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(0, 3, kMipLevels), 3U);

  // Layer 1: mips 0-3
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(1, 0, kMipLevels), 4U);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(1, 1, kMipLevels), 5U);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(1, 2, kMipLevels), 6U);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(1, 3, kMipLevels), 7U);
}

//! ComputeMipDimension halves correctly with minimum of 1.
NOLINT_TEST(ScratchImageBasicTest, ComputeMipDimensionHalvesCorrectly)
{
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 0), 1024U);
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 1), 512U);
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 2), 256U);
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 10), 1U);
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 11), 1U); // Clamped to 1
}

//=== ScratchImage Create Tests ===------------------------------------------//

//! Test fixture for ScratchImage::Create tests.
//! Create with valid metadata produces a valid image.
NOLINT_TEST(ScratchImageCreateTest, ValidMetadataCreatesValidImage)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 256,
    .height = 256,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  auto image = ScratchImage::Create(meta);

  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.Meta().width, 256U);
  EXPECT_EQ(image.Meta().height, 256U);
  EXPECT_EQ(image.Meta().format, Format::kRGBA8UNorm);
  EXPECT_EQ(image.GetSubresourceCount(), 1U);

  // RGBA8 = 4 bytes per pixel, 256x256 = 262144 bytes
  EXPECT_EQ(image.GetTotalSizeBytes(), 256U * 256U * 4U);
}

//! Create with multiple mip levels allocates correct storage.
NOLINT_TEST(ScratchImageCreateTest, MultipleMipsAllocatesCorrectStorage)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 4, // 64x64, 32x32, 16x16, 8x8
    .format = Format::kRGBA8UNorm,
  };

  auto image = ScratchImage::Create(meta);

  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.GetSubresourceCount(), 4U);

  // Total size = 64*64*4 + 32*32*4 + 16*16*4 + 8*8*4
  //            = 16384 + 4096 + 1024 + 256 = 21760
  EXPECT_EQ(image.GetTotalSizeBytes(), 21760U);
}

//! Create with array layers allocates correct storage.
NOLINT_TEST(ScratchImageCreateTest, ArrayTextureAllocatesCorrectStorage)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2DArray,
    .width = 32,
    .height = 32,
    .depth = 1,
    .array_layers = 4,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  auto image = ScratchImage::Create(meta);

  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.GetSubresourceCount(), 4U);
  EXPECT_EQ(image.GetTotalSizeBytes(), 32U * 32U * 4U * 4U); // 16384 bytes
}

//! Create with zero dimensions returns invalid image.
NOLINT_TEST(ScratchImageCreateTest, ZeroDimensionsReturnsInvalidImage)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 0,
    .height = 0,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  auto image = ScratchImage::Create(meta);

  EXPECT_FALSE(image.IsValid());
}

//=== ScratchImage CreateFromData Tests ===----------------------------------//

//! Test fixture for ScratchImage::CreateFromData tests.
//! CreateFromData wraps existing pixel data correctly.
NOLINT_TEST(ScratchImageCreateFromDataTest, ValidDataCreatesImageWithData)
{
  constexpr uint32_t kWidth = 4;
  constexpr uint32_t kHeight = 4;
  constexpr uint32_t kBpp = 4;
  constexpr uint32_t kRowPitch = kWidth * kBpp;

  std::vector<std::byte> pixels(kWidth * kHeight * kBpp);
  // Fill with test pattern: each pixel has its index as value
  for (size_t i = 0; i < pixels.size(); ++i) {
    pixels.at(i) = static_cast<std::byte>(i & 0xFF);
  }

  auto image = ScratchImage::CreateFromData(
    kWidth, kHeight, Format::kRGBA8UNorm, kRowPitch, std::move(pixels));

  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.Meta().width, kWidth);
  EXPECT_EQ(image.Meta().height, kHeight);
  EXPECT_EQ(image.Meta().mip_levels, 1U);
  EXPECT_EQ(image.Meta().array_layers, 1U);
  EXPECT_EQ(image.GetTotalSizeBytes(), kWidth * kHeight * kBpp);
}

//=== ScratchImage GetImage Tests ===----------------------------------------//

//! Test fixture for ScratchImage::GetImage tests.
//! GetImage returns correct view for mip 0.
NOLINT_TEST(ScratchImageGetImageTest, Mip0ReturnsCorrectView)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 128,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };
  auto image = ScratchImage::Create(meta);

  const ImageView view = image.GetImage(0, 0);

  EXPECT_EQ(view.width, 128U);
  EXPECT_EQ(view.height, 64U);
  EXPECT_EQ(view.format, Format::kRGBA8UNorm);
  EXPECT_EQ(view.row_pitch_bytes, 128U * 4U); // 512 bytes per row
  EXPECT_EQ(view.pixels.size(), 128U * 64U * 4U); // 32768 bytes total
}

//! GetImage returns correct dimensions for different mip levels.
NOLINT_TEST(ScratchImageGetImageTest, DifferentMipsReturnsCorrectDimensions)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 4,
    .format = Format::kRGBA8UNorm,
  };
  auto image = ScratchImage::Create(meta);

  const auto view0 = image.GetImage(0, 0);
  EXPECT_EQ(view0.width, 64U);
  EXPECT_EQ(view0.height, 64U);

  const auto view1 = image.GetImage(0, 1);
  EXPECT_EQ(view1.width, 32U);
  EXPECT_EQ(view1.height, 32U);

  const auto view2 = image.GetImage(0, 2);
  EXPECT_EQ(view2.width, 16U);
  EXPECT_EQ(view2.height, 16U);

  const auto view3 = image.GetImage(0, 3);
  EXPECT_EQ(view3.width, 8U);
  EXPECT_EQ(view3.height, 8U);
}

//! GetImage returns correct views for array layers.
NOLINT_TEST(ScratchImageGetImageTest, ArrayLayersReturnsDistinctViews)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2DArray,
    .width = 16,
    .height = 16,
    .depth = 1,
    .array_layers = 3,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };
  auto image = ScratchImage::Create(meta);

  const auto view0 = image.GetImage(0, 0);
  const auto view1 = image.GetImage(1, 0);
  const auto view2 = image.GetImage(2, 0);

  // Each view should have same dimensions but different pixel spans
  EXPECT_EQ(view0.width, 16U);
  EXPECT_EQ(view1.width, 16U);
  EXPECT_EQ(view2.width, 16U);

  // Pixel spans should point to different memory locations
  EXPECT_NE(view0.pixels.data(), view1.pixels.data());
  EXPECT_NE(view1.pixels.data(), view2.pixels.data());
  EXPECT_NE(view0.pixels.data(), view2.pixels.data());
}

//=== ScratchImage GetMutablePixels Tests ===---------------------------------//

//! Test fixture for ScratchImage::GetMutablePixels tests.
//! GetMutablePixels allows writing to pixel data.
NOLINT_TEST(ScratchImageGetMutablePixelsTest, WritePixelsDataIsPersisted)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 2,
    .height = 2,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };
  auto image = ScratchImage::Create(meta);

  // Write test pattern
  auto pixels = image.GetMutablePixels(0, 0);
  for (size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::byte>(i);
  }

  // Verify via GetImage
  const ImageView view = image.GetImage(0, 0);
  EXPECT_EQ(view.pixels[0], std::byte { 0 });
  EXPECT_EQ(view.pixels[1], std::byte { 1 });
  EXPECT_EQ(view.pixels[2], std::byte { 2 });
  EXPECT_EQ(view.pixels[3], std::byte { 3 });
}

//=== ScratchImage Format Tests ===------------------------------------------//

//! Test fixture for ScratchImage format-specific tests.
//! Single-channel R8 format allocates correct size.
NOLINT_TEST(ScratchImageFormatTest, R8FormatAllocatesCorrectSize)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kR8UNorm,
  };

  auto image = ScratchImage::Create(meta);

  EXPECT_EQ(image.GetTotalSizeBytes(), 64U * 64U * 1U); // 4096 bytes
}

//! RGBA16F format allocates correct size (8 bytes per pixel).
NOLINT_TEST(ScratchImageFormatTest, RGBA16FFormatAllocatesCorrectSize)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 32,
    .height = 32,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA16Float,
  };

  auto image = ScratchImage::Create(meta);

  EXPECT_EQ(image.GetTotalSizeBytes(), 32U * 32U * 8U); // 8192 bytes
}

//! RGBA32F format allocates correct size (16 bytes per pixel).
NOLINT_TEST(ScratchImageFormatTest, RGBA32FFormatAllocatesCorrectSize)
{
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 16,
    .height = 16,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA32Float,
  };

  auto image = ScratchImage::Create(meta);

  EXPECT_EQ(image.GetTotalSizeBytes(), 16U * 16U * 16U); // 4096 bytes
}

} // namespace
