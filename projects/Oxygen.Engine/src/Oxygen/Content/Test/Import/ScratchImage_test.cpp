//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/ScratchImage.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::ImageView;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::ScratchImageMeta;

//=== ScratchImage Basic Tests ===-------------------------------------------//

//! Test fixture for basic ScratchImage tests.
class ScratchImageBasicTest : public ::testing::Test { };

//! Default-constructed ScratchImage should be invalid.
NOLINT_TEST_F(ScratchImageBasicTest, DefaultConstruction_CreatesInvalidImage)
{
  // Arrange & Act
  const ScratchImage image;

  // Assert
  EXPECT_FALSE(image.IsValid());
  EXPECT_EQ(image.GetTotalSizeBytes(), 0u);
  EXPECT_EQ(image.GetSubresourceCount(), 0u);
}

//! ComputeMipCount returns correct values for various dimensions.
NOLINT_TEST_F(ScratchImageBasicTest, ComputeMipCount_ReturnsCorrectValues)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ScratchImage::ComputeMipCount(1, 1), 1u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(2, 2), 2u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(4, 4), 3u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(8, 8), 4u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(16, 16), 5u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(256, 256), 9u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(1024, 1024), 11u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(2048, 2048), 12u);
}

//! ComputeMipCount handles non-square textures correctly.
NOLINT_TEST_F(ScratchImageBasicTest, ComputeMipCount_NonSquareTextures)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ScratchImage::ComputeMipCount(1024, 512), 11u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(512, 1024), 11u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(4, 1), 3u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(1, 4), 3u);
}

//! ComputeMipCount returns 0 for zero dimensions.
NOLINT_TEST_F(ScratchImageBasicTest, ComputeMipCount_ZeroDimensions)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ScratchImage::ComputeMipCount(0, 0), 0u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(0, 100), 0u);
  EXPECT_EQ(ScratchImage::ComputeMipCount(100, 0), 0u);
}

//! ComputeSubresourceIndex follows layer-major ordering.
NOLINT_TEST_F(ScratchImageBasicTest, ComputeSubresourceIndex_LayerMajorOrdering)
{
  // Arrange
  constexpr uint16_t kMipLevels = 4;

  // Act & Assert
  // Layer 0: mips 0-3
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(0, 0, kMipLevels), 0u);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(0, 1, kMipLevels), 1u);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(0, 2, kMipLevels), 2u);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(0, 3, kMipLevels), 3u);

  // Layer 1: mips 0-3
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(1, 0, kMipLevels), 4u);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(1, 1, kMipLevels), 5u);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(1, 2, kMipLevels), 6u);
  EXPECT_EQ(ScratchImage::ComputeSubresourceIndex(1, 3, kMipLevels), 7u);
}

//! ComputeMipDimension halves correctly with minimum of 1.
NOLINT_TEST_F(ScratchImageBasicTest, ComputeMipDimension_HalvesCorrectly)
{
  // Arrange & Act & Assert
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 0), 1024u);
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 1), 512u);
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 2), 256u);
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 10), 1u);
  EXPECT_EQ(ScratchImage::ComputeMipDimension(1024, 11), 1u); // Clamped to 1
}

//=== ScratchImage Create Tests ===------------------------------------------//

//! Test fixture for ScratchImage::Create tests.
class ScratchImageCreateTest : public ::testing::Test { };

//! Create with valid metadata produces a valid image.
NOLINT_TEST_F(ScratchImageCreateTest, ValidMetadata_CreatesValidImage)
{
  // Arrange
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 256,
    .height = 256,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto image = ScratchImage::Create(meta);

  // Assert
  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.Meta().width, 256u);
  EXPECT_EQ(image.Meta().height, 256u);
  EXPECT_EQ(image.Meta().format, Format::kRGBA8UNorm);
  EXPECT_EQ(image.GetSubresourceCount(), 1u);

  // RGBA8 = 4 bytes per pixel, 256x256 = 262144 bytes
  EXPECT_EQ(image.GetTotalSizeBytes(), 256u * 256u * 4u);
}

//! Create with multiple mip levels allocates correct storage.
NOLINT_TEST_F(ScratchImageCreateTest, MultipleMips_AllocatesCorrectStorage)
{
  // Arrange
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 4, // 64x64, 32x32, 16x16, 8x8
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto image = ScratchImage::Create(meta);

  // Assert
  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.GetSubresourceCount(), 4u);

  // Total size = 64*64*4 + 32*32*4 + 16*16*4 + 8*8*4
  //            = 16384 + 4096 + 1024 + 256 = 21760
  EXPECT_EQ(image.GetTotalSizeBytes(), 21760u);
}

//! Create with array layers allocates correct storage.
NOLINT_TEST_F(ScratchImageCreateTest, ArrayTexture_AllocatesCorrectStorage)
{
  // Arrange
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2DArray,
    .width = 32,
    .height = 32,
    .depth = 1,
    .array_layers = 4,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto image = ScratchImage::Create(meta);

  // Assert
  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.GetSubresourceCount(), 4u);
  EXPECT_EQ(image.GetTotalSizeBytes(), 32u * 32u * 4u * 4u); // 16384 bytes
}

//! Create with zero dimensions returns invalid image.
NOLINT_TEST_F(ScratchImageCreateTest, ZeroDimensions_ReturnsInvalidImage)
{
  // Arrange
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 0,
    .height = 0,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  };

  // Act
  auto image = ScratchImage::Create(meta);

  // Assert
  EXPECT_FALSE(image.IsValid());
}

//=== ScratchImage CreateFromData Tests ===----------------------------------//

//! Test fixture for ScratchImage::CreateFromData tests.
class ScratchImageCreateFromDataTest : public ::testing::Test { };

//! CreateFromData wraps existing pixel data correctly.
NOLINT_TEST_F(ScratchImageCreateFromDataTest, ValidData_CreatesImageWithData)
{
  // Arrange
  constexpr uint32_t kWidth = 4;
  constexpr uint32_t kHeight = 4;
  constexpr uint32_t kBpp = 4;
  constexpr uint32_t kRowPitch = kWidth * kBpp;

  std::vector<std::byte> pixels(kWidth * kHeight * kBpp);
  // Fill with test pattern: each pixel has its index as value
  for (size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::byte>(i & 0xFF);
  }

  // Act
  auto image = ScratchImage::CreateFromData(
    kWidth, kHeight, Format::kRGBA8UNorm, kRowPitch, std::move(pixels));

  // Assert
  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.Meta().width, kWidth);
  EXPECT_EQ(image.Meta().height, kHeight);
  EXPECT_EQ(image.Meta().mip_levels, 1u);
  EXPECT_EQ(image.Meta().array_layers, 1u);
  EXPECT_EQ(image.GetTotalSizeBytes(), kWidth * kHeight * kBpp);
}

//=== ScratchImage GetImage Tests ===----------------------------------------//

//! Test fixture for ScratchImage::GetImage tests.
class ScratchImageGetImageTest : public ::testing::Test { };

//! GetImage returns correct view for mip 0.
NOLINT_TEST_F(ScratchImageGetImageTest, Mip0_ReturnsCorrectView)
{
  // Arrange
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

  // Act
  const ImageView view = image.GetImage(0, 0);

  // Assert
  EXPECT_EQ(view.width, 128u);
  EXPECT_EQ(view.height, 64u);
  EXPECT_EQ(view.format, Format::kRGBA8UNorm);
  EXPECT_EQ(view.row_pitch_bytes, 128u * 4u); // 512 bytes per row
  EXPECT_EQ(view.pixels.size(), 128u * 64u * 4u); // 32768 bytes total
}

//! GetImage returns correct dimensions for different mip levels.
NOLINT_TEST_F(ScratchImageGetImageTest, DifferentMips_ReturnsCorrectDimensions)
{
  // Arrange
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

  // Act & Assert
  const auto view0 = image.GetImage(0, 0);
  EXPECT_EQ(view0.width, 64u);
  EXPECT_EQ(view0.height, 64u);

  const auto view1 = image.GetImage(0, 1);
  EXPECT_EQ(view1.width, 32u);
  EXPECT_EQ(view1.height, 32u);

  const auto view2 = image.GetImage(0, 2);
  EXPECT_EQ(view2.width, 16u);
  EXPECT_EQ(view2.height, 16u);

  const auto view3 = image.GetImage(0, 3);
  EXPECT_EQ(view3.width, 8u);
  EXPECT_EQ(view3.height, 8u);
}

//! GetImage returns correct views for array layers.
NOLINT_TEST_F(ScratchImageGetImageTest, ArrayLayers_ReturnsDistinctViews)
{
  // Arrange
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

  // Act
  const auto view0 = image.GetImage(0, 0);
  const auto view1 = image.GetImage(1, 0);
  const auto view2 = image.GetImage(2, 0);

  // Assert - each view should have same dimensions but different pixel spans
  EXPECT_EQ(view0.width, 16u);
  EXPECT_EQ(view1.width, 16u);
  EXPECT_EQ(view2.width, 16u);

  // Pixel spans should point to different memory locations
  EXPECT_NE(view0.pixels.data(), view1.pixels.data());
  EXPECT_NE(view1.pixels.data(), view2.pixels.data());
  EXPECT_NE(view0.pixels.data(), view2.pixels.data());
}

//=== ScratchImage GetMutablePixels Tests ===---------------------------------//

//! Test fixture for ScratchImage::GetMutablePixels tests.
class ScratchImageGetMutablePixelsTest : public ::testing::Test { };

//! GetMutablePixels allows writing to pixel data.
NOLINT_TEST_F(ScratchImageGetMutablePixelsTest, WritePixels_DataIsPersisted)
{
  // Arrange
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

  // Act - write test pattern
  auto pixels = image.GetMutablePixels(0, 0);
  for (size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::byte>(i);
  }

  // Assert - verify via GetImage
  const ImageView view = image.GetImage(0, 0);
  EXPECT_EQ(view.pixels[0], std::byte { 0 });
  EXPECT_EQ(view.pixels[1], std::byte { 1 });
  EXPECT_EQ(view.pixels[2], std::byte { 2 });
  EXPECT_EQ(view.pixels[3], std::byte { 3 });
}

//=== ScratchImage Format Tests ===------------------------------------------//

//! Test fixture for ScratchImage format-specific tests.
class ScratchImageFormatTest : public ::testing::Test { };

//! Single-channel R8 format allocates correct size.
NOLINT_TEST_F(ScratchImageFormatTest, R8Format_AllocatesCorrectSize)
{
  // Arrange
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kR8UNorm,
  };

  // Act
  auto image = ScratchImage::Create(meta);

  // Assert
  EXPECT_EQ(image.GetTotalSizeBytes(), 64u * 64u * 1u); // 4096 bytes
}

//! RGBA16F format allocates correct size (8 bytes per pixel).
NOLINT_TEST_F(ScratchImageFormatTest, RGBA16FFormat_AllocatesCorrectSize)
{
  // Arrange
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 32,
    .height = 32,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA16Float,
  };

  // Act
  auto image = ScratchImage::Create(meta);

  // Assert
  EXPECT_EQ(image.GetTotalSizeBytes(), 32u * 32u * 8u); // 8192 bytes
}

//! RGBA32F format allocates correct size (16 bytes per pixel).
NOLINT_TEST_F(ScratchImageFormatTest, RGBA32FFormat_AllocatesCorrectSize)
{
  // Arrange
  const ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 16,
    .height = 16,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA32Float,
  };

  // Act
  auto image = ScratchImage::Create(meta);

  // Assert
  EXPECT_EQ(image.GetTotalSizeBytes(), 16u * 16u * 16u); // 4096 bytes
}

} // namespace
