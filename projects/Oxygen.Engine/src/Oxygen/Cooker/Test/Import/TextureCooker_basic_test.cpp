//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/Internal/TextureCooker.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportDesc.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/PakFormat.h>

namespace {

using oxygen::ColorSpace;
using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::Bc7Quality;
using oxygen::content::import::CookedTexturePayload;
using oxygen::content::import::CookTexture;
using oxygen::content::import::D3D12PackingPolicy;
using oxygen::content::import::HdrHandling;
using oxygen::content::import::MipFilter;
using oxygen::content::import::MipPolicy;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::ScratchImageMeta;
using oxygen::content::import::TextureImportDesc;
using oxygen::content::import::TextureImportError;
using oxygen::content::import::TextureIntent;
using oxygen::content::import::TextureSourceSet;
using oxygen::content::import::TightPackedPolicy;

//===----------------------------------------------------------------------===//
// Test Utilities
//===----------------------------------------------------------------------===//

//! Creates a minimal valid BMP image (2x2, 32-bit BGRA).
/*!
 \return A byte vector containing a valid BMP file with 4 colored pixels.
*/
[[nodiscard]] auto MakeBmp2x2() -> std::vector<std::byte>
{
  // BMP file header (14 bytes) + DIB header (40 bytes) + 4 pixels (16 bytes)
  constexpr uint32_t kFileSize = 14u + 40u + 16u;
  constexpr uint32_t kPixelOffset = 54u;
  constexpr uint32_t kDibHeaderSize = 40u;
  constexpr int32_t kWidth = 2;
  constexpr int32_t kHeight = 2;
  constexpr uint16_t kPlanes = 1u;
  constexpr uint16_t kBitsPerPixel = 32u;

  std::vector<std::byte> bytes;
  bytes.reserve(kFileSize);

  // Helper lambdas to append little-endian values
  const auto push_u16 = [&bytes](uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
  };
  const auto push_u32 = [&bytes](uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
  };
  const auto push_i32 = [&bytes](int32_t value) {
    const auto unsigned_val = static_cast<uint32_t>(value);
    bytes.push_back(static_cast<std::byte>(unsigned_val & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 24u) & 0xFFu));
  };
  const auto push_bgra
    = [&bytes](uint8_t blue, uint8_t green, uint8_t red, uint8_t alpha) {
        bytes.push_back(static_cast<std::byte>(blue));
        bytes.push_back(static_cast<std::byte>(green));
        bytes.push_back(static_cast<std::byte>(red));
        bytes.push_back(static_cast<std::byte>(alpha));
      };

  // BMP file header (14 bytes)
  bytes.push_back(static_cast<std::byte>('B')); // Signature
  bytes.push_back(static_cast<std::byte>('M'));
  push_u32(kFileSize); // File size
  push_u16(0u); // Reserved
  push_u16(0u); // Reserved
  push_u32(kPixelOffset); // Pixel data offset

  // DIB header (BITMAPINFOHEADER, 40 bytes)
  push_u32(kDibHeaderSize); // Header size
  push_i32(kWidth); // Width
  push_i32(kHeight); // Height (positive = bottom-up)
  push_u16(kPlanes); // Color planes
  push_u16(kBitsPerPixel); // Bits per pixel
  push_u32(0u); // Compression (none)
  push_u32(16u); // Image size (4 pixels * 4 bytes)
  push_i32(2835); // Horizontal resolution (72 DPI)
  push_i32(2835); // Vertical resolution (72 DPI)
  push_u32(0u); // Colors in palette
  push_u32(0u); // Important colors

  // Pixel data (bottom-up, BGRA format)
  // Row 0 (bottom): red, white
  push_bgra(0u, 0u, 255u, 255u); // Red
  push_bgra(255u, 255u, 255u, 255u); // White
  // Row 1 (top): blue, green
  push_bgra(255u, 0u, 0u, 255u); // Blue
  push_bgra(0u, 255u, 0u, 255u); // Green

  return bytes;
}

//! Creates a minimal valid BMP image (1x1, 32-bit BGRA).
/*!

eturn A byte vector containing a valid BMP file with 1 colored pixel.
*/
[[nodiscard]] auto MakeBmp1x1() -> std::vector<std::byte>
{
  // BMP file header (14 bytes) + DIB header (40 bytes) + 1 pixel (4 bytes)
  constexpr uint32_t kFileSize = 14u + 40u + 4u;
  constexpr uint32_t kPixelOffset = 54u;
  constexpr uint32_t kDibHeaderSize = 40u;
  constexpr int32_t kWidth = 1;
  constexpr int32_t kHeight = 1;
  constexpr uint16_t kPlanes = 1u;
  constexpr uint16_t kBitsPerPixel = 32u;

  std::vector<std::byte> bytes;
  bytes.reserve(kFileSize);

  // Helper lambdas to append little-endian values
  const auto push_u16 = [&bytes](uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
  };
  const auto push_u32 = [&bytes](uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
  };
  const auto push_i32 = [&bytes](int32_t value) {
    const auto unsigned_val = static_cast<uint32_t>(value);
    bytes.push_back(static_cast<std::byte>(unsigned_val & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((unsigned_val >> 24u) & 0xFFu));
  };
  const auto push_bgra
    = [&bytes](uint8_t blue, uint8_t green, uint8_t red, uint8_t alpha) {
        bytes.push_back(static_cast<std::byte>(blue));
        bytes.push_back(static_cast<std::byte>(green));
        bytes.push_back(static_cast<std::byte>(red));
        bytes.push_back(static_cast<std::byte>(alpha));
      };

  // BMP file header (14 bytes)
  bytes.push_back(static_cast<std::byte>('B')); // Signature
  bytes.push_back(static_cast<std::byte>('M'));
  push_u32(kFileSize); // File size
  push_u16(0u); // Reserved
  push_u16(0u); // Reserved
  push_u32(kPixelOffset); // Pixel data offset

  // DIB header (BITMAPINFOHEADER, 40 bytes)
  push_u32(kDibHeaderSize); // Header size
  push_i32(kWidth); // Width
  push_i32(kHeight); // Height (positive = bottom-up)
  push_u16(kPlanes); // Color planes
  push_u16(kBitsPerPixel); // Bits per pixel
  push_u32(0u); // Compression (none)
  push_u32(4u); // Image size (1 pixel * 4 bytes)
  push_i32(2835); // Horizontal resolution (72 DPI)
  push_i32(2835); // Vertical resolution (72 DPI)
  push_u32(0u); // Colors in palette
  push_u32(0u); // Important colors

  // Pixel data (bottom-up, BGRA format)
  push_bgra(10u, 20u, 30u, 255u); // Single pixel

  return bytes;
}

//! Returns the test BMP image as a span of bytes.
[[nodiscard]] auto GetTestImageBytes() -> std::span<const std::byte>
{
  static const auto kTestBmp = MakeBmp2x2();
  return { kTestBmp.data(), kTestBmp.size() };
}

//! Returns the 1x1 BMP test image as a span of bytes.
[[nodiscard]] auto GetTestImageBytes1x1() -> std::span<const std::byte>
{
  static const auto kTestBmp = MakeBmp1x1();
  return { kTestBmp.data(), kTestBmp.size() };
}

//! Creates a minimal 2x2 RGBA32Float ScratchImage.
/*!

eturn A ScratchImage containing a 2x2 RGBA32Float image.
*/
[[nodiscard]] auto MakeFloatImage2x2() -> ScratchImage
{
  ScratchImageMeta meta {
    .texture_type = TextureType::kTexture2D,
    .width = 2,
    .height = 2,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA32Float,
  };

  ScratchImage image = ScratchImage::Create(meta);
  auto pixels = image.GetMutablePixels(0, 0);
  const std::array<float, 16> rgba = {
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
  };
  std::memcpy(pixels.data(), rgba.data(), sizeof(rgba));
  return image;
}

//===----------------------------------------------------------------------===//
// Validation Tests (6.2)
//===----------------------------------------------------------------------===//

NOLINT_TEST(
  TextureCookerColorTransferTest, ConvertsDeclaredSourceToStoredEncoding)
{
  constexpr std::array<std::uint8_t, 4> source { 128, 64, 10, 192 };
  const auto decode = [](double value) {
    return value <= .04045 ? value / 12.92
                           : std::pow((value + .055) / 1.055, 2.4);
  };
  const auto encode = [](double value) {
    return value <= .0031308 ? value * 12.92
                             : 1.055 * std::pow(value, 1 / 2.4) - .055;
  };
  for (const auto source_space : { ColorSpace::kLinear, ColorSpace::kSRGB })
    for (const auto output : { Format::kRGBA32Float, Format::kRGBA16Float,
           Format::kRGBA8UNorm, Format::kRGBA8UNormSRGB }) {
      SCOPED_TRACE(static_cast<int>(source_space));
      SCOPED_TRACE(static_cast<int>(output));
      auto image = ScratchImage::Create(
        { .width = 1, .height = 1, .format = Format::kRGBA8UNorm });
      std::memcpy(
        image.GetMutablePixels(0, 0).data(), source.data(), source.size());
      auto desc = TextureImportDesc {};
      desc.intent = TextureIntent::kEmissive;
      desc.source_color_space = source_space;
      desc.output_format = output;
      desc.mip_policy = MipPolicy::kNone;
      const auto cooked
        = CookTexture(std::move(image), desc, TightPackedPolicy::Instance());
      ASSERT_TRUE(cooked.has_value()) << static_cast<int>(cooked.error());
      EXPECT_EQ(cooked->desc.format, output);
      oxygen::data::pak::render::TexturePayloadHeader header {};
      std::memcpy(&header, cooked->payload.data(), sizeof(header));
      const auto* data = cooked->payload.data() + header.data_offset_bytes
        + cooked->layouts[0].offset_bytes;
      for (unsigned c = 0; c < 4; ++c) {
        const double normalized = double(source[c]) / 255;
        const double linear = c < 3 && source_space == ColorSpace::kSRGB
          ? decode(normalized)
          : normalized;
        const double expected = c < 3 && output == Format::kRGBA8UNormSRGB
          ? encode(linear)
          : linear;
        double actual = 0, tolerance = 2e-6;
        if (output == Format::kRGBA32Float) {
          float value;
          std::memcpy(&value, data + c * 4, 4);
          actual = value;
        } else if (output == Format::kRGBA16Float) {
          std::uint16_t bits;
          std::memcpy(&bits, data + c * 2, 2);
          actual = oxygen::data::HalfFloat { bits }.ToFloat();
          tolerance = expected * .0006 + 0x1p-24;
        } else {
          actual = double(std::to_integer<unsigned>(data[c])) / 255;
          tolerance = .5 / 255 + 1e-6;
        }
        EXPECT_NEAR(actual, expected, tolerance) << "channel=" << c;
      }
    }
}

NOLINT_TEST(TextureCookerColorTransferTest, FiltersColorMipsInLinearLight)
{
  for (const auto source_space : { ColorSpace::kLinear, ColorSpace::kSRGB })
    for (const std::uint8_t dark :
      { std::uint8_t { 0 }, std::uint8_t { 128 } }) {
      const std::array<std::uint8_t, 16> pixels { dark, dark, dark, 64, 255,
        255, 255, 64, dark, dark, dark, 64, 255, 255, 255, 64 };
      auto image = ScratchImage::Create(
        { .width = 2, .height = 2, .format = Format::kRGBA8UNorm });
      std::memcpy(
        image.GetMutablePixels(0, 0).data(), pixels.data(), pixels.size());
      auto desc = TextureImportDesc {};
      desc.intent = TextureIntent::kEmissive;
      desc.source_color_space = source_space;
      desc.output_format = Format::kRGBA32Float;
      desc.mip_filter = MipFilter::kBox;
      const auto cooked
        = CookTexture(std::move(image), desc, TightPackedPolicy::Instance());
      ASSERT_TRUE(cooked.has_value()) << static_cast<int>(cooked.error());
      ASSERT_EQ(cooked->desc.mip_levels, 2U);
      oxygen::data::pak::render::TexturePayloadHeader header {};
      std::memcpy(&header, cooked->payload.data(), sizeof(header));
      std::array<float, 4> actual {};
      std::memcpy(actual.data(),
        cooked->payload.data() + header.data_offset_bytes
          + cooked->layouts[1].offset_bytes,
        sizeof(actual));
      const double code = double(dark) / 255;
      const double linear = source_space == ColorSpace::kLinear ? code
        : code <= .04045                                        ? code / 12.92
                         : std::pow((code + .055) / 1.055, 2.4);
      const double expected = .5 * (linear + 1);
      for (unsigned c = 0; c < 3; ++c)
        EXPECT_NEAR(actual[c], expected, 2e-6);
      EXPECT_NEAR(actual[3], 64.0 / 255, 1e-7);
    }
}

NOLINT_TEST(
  TextureCookerColorTransferTest, DarkColorMipsQuantizeOnlyAtFinalStorage)
{
  for (const unsigned size : { 2U, 4U }) {
    auto image = ScratchImage::Create(
      { .width = size, .height = size, .format = Format::kRGBA8UNorm });
    auto pixels = image.GetMutablePixels(0, 0);
    for (unsigned pixel = 0; pixel < size * size; ++pixel) {
      for (unsigned c = 0; c < 3; ++c)
        pixels[pixel * 4 + c]
          = std::byte { static_cast<unsigned char>(pixel == 0 ? 1 : 0) };
      pixels[pixel * 4 + 3] = std::byte { 128 };
    }
    auto desc = TextureImportDesc {};
    desc.intent = TextureIntent::kEmissive;
    desc.source_color_space = ColorSpace::kLinear;
    desc.mip_filter = MipFilter::kBox;
    desc.output_format = Format::kRGBA8UNormSRGB;
    const auto cooked
      = CookTexture(std::move(image), desc, TightPackedPolicy::Instance());
    ASSERT_TRUE(cooked.has_value()) << static_cast<int>(cooked.error());
    ASSERT_EQ(cooked->desc.mip_levels, size == 2 ? 2 : 3);
    oxygen::data::pak::render::TexturePayloadHeader header {};
    std::memcpy(&header, cooked->payload.data(), sizeof(header));
    for (unsigned mip = 1; mip < cooked->desc.mip_levels; ++mip) {
      const double linear = 1.0 / (255.0 * double(1U << (mip * 2)));
      const auto expected
        = static_cast<unsigned>(std::floor(255.0 * 12.92 * linear + .5));
      EXPECT_GT(expected, 0U);
      const auto width = size >> mip;
      const auto& layout = cooked->layouts[mip];
      const auto* data = cooked->payload.data() + header.data_offset_bytes
        + layout.offset_bytes;
      for (unsigned y = 0; y < width; ++y)
        for (unsigned x = 0; x < width; ++x) {
          const auto* pixel = data + y * layout.row_pitch_bytes + x * 4;
          for (unsigned c = 0; c < 3; ++c)
            EXPECT_EQ(std::to_integer<unsigned>(pixel[c]),
              x == 0 && y == 0 ? expected : 0U);
          EXPECT_EQ(std::to_integer<unsigned>(pixel[3]), 128U);
        }
    }
  }
}

class TextureCookerValidationTest : public ::testing::Test { };

//! Test: CookTexture rejects zero dimensions.
NOLINT_TEST_F(TextureCookerValidationTest, RejectsZeroDimensions)
{
  // Arrange
  TextureImportDesc desc;
  desc.width = 0;
  desc.height = 64;
  desc.output_format = Format::kRGBA8UNorm;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidDimensions);
}

//! Test: CookTexture rejects depth for 2D texture.
NOLINT_TEST_F(TextureCookerValidationTest, RejectsDepthFor2D)
{
  // Arrange
  TextureImportDesc desc;
  desc.width = 64;
  desc.height = 64;
  desc.depth = 4; // Invalid for 2D
  desc.texture_type = TextureType::kTexture2DArray;
  desc.output_format = Format::kRGBA8UNorm;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kDepthInvalidFor2D);
}

//===----------------------------------------------------------------------===//
// Decode Tests
//===----------------------------------------------------------------------===//

class TextureCookerDecodeTest : public ::testing::Test { };

//! Test: CookTexture fails on invalid image data.
NOLINT_TEST_F(TextureCookerDecodeTest, FailsOnInvalidData)
{
  // Arrange
  std::vector<std::byte> garbage(100, std::byte { 0xAB });

  TextureImportDesc desc;
  desc.width = 64;
  desc.height = 64;
  desc.output_format = Format::kRGBA8UNorm;

  // Act
  auto result = CookTexture(garbage, desc, TightPackedPolicy::Instance());

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_TRUE(oxygen::content::import::IsDecodeError(result.error()));
}

//===----------------------------------------------------------------------===//
// Basic Cooking Tests (6.3)
//===----------------------------------------------------------------------===//

class TextureCookerBasicTest : public ::testing::Test { };

//! Test: CookTexture produces valid output for minimal BMP.
NOLINT_TEST_F(TextureCookerBasicTest, CooksMinimalBmp)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.texture_type = TextureType::kTexture2DArray;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(result.has_value())
    << "Error: " << static_cast<int>(result.error());

  const auto& payload = *result;
  EXPECT_EQ(payload.desc.width, 2u);
  EXPECT_EQ(payload.desc.height, 2u);
  EXPECT_EQ(payload.desc.format, Format::kRGBA8UNorm);
  EXPECT_EQ(payload.desc.mip_levels, 1u);
  EXPECT_FALSE(payload.payload.empty());
  EXPECT_NE(payload.desc.content_hash, 0u);
}

//! Test: CookTexture sets packing policy ID.
NOLINT_TEST_F(TextureCookerBasicTest, SetsPackingPolicyId)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto d3d12_result
    = CookTexture(GetTestImageBytes(), desc, D3D12PackingPolicy::Instance());
  auto tight_result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(d3d12_result.has_value());
  ASSERT_TRUE(tight_result.has_value());
  EXPECT_EQ(d3d12_result->desc.packing_policy_id, "d3d12");
  EXPECT_EQ(tight_result->desc.packing_policy_id, "tight");
}

//! Test: Content hash is deterministic.
NOLINT_TEST_F(TextureCookerBasicTest, ContentHashIsDeterministic)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result1
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());
  auto result2
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(result1.has_value());
  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(result1->desc.content_hash, result2->desc.content_hash);
}

//===----------------------------------------------------------------------===//
// D3D12 vs TightPacked Layout Tests (6.3.10)
//===----------------------------------------------------------------------===//

class TextureCookerLayoutTest : public ::testing::Test { };

//! Test: D3D12 packing produces aligned row pitch.
NOLINT_TEST_F(TextureCookerLayoutTest, D3D12ProducesAlignedLayout)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, D3D12PackingPolicy::Instance());

  // Assert
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->layouts.size(), 1u);

  // D3D12 aligns row pitch to 256
  EXPECT_EQ(result->layouts[0].row_pitch_bytes % 256u, 0u);
}

//! Test: TightPacked minimizes payload size.
NOLINT_TEST_F(TextureCookerLayoutTest, TightPackedMinimizesSize)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto d3d12_result
    = CookTexture(GetTestImageBytes(), desc, D3D12PackingPolicy::Instance());
  auto tight_result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(d3d12_result.has_value());
  ASSERT_TRUE(tight_result.has_value());

  // Tight should be smaller or equal (never larger)
  EXPECT_LE(tight_result->payload.size(), d3d12_result->payload.size());
}

//===----------------------------------------------------------------------===//
// Mip Generation Tests (6.3.2)
//===----------------------------------------------------------------------===//

class TextureCookerMipTest : public ::testing::Test { };

//! Test: CookTexture generates mip chain when requested.
NOLINT_TEST_F(TextureCookerMipTest, GeneratesFullMipChain)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kFullChain;
  desc.mip_filter = MipFilter::kBox;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(result.has_value())
    << "Error: " << static_cast<int>(result.error());

  // 2x2 image should have 2 mip levels (2x2 -> 1x1)
  EXPECT_EQ(result->desc.mip_levels, 2u);
  EXPECT_EQ(result->layouts.size(), 2u);
}

//! Test: CookTexture respects max_mip_levels limit.
NOLINT_TEST_F(TextureCookerMipTest, RespectsMaxMipLevels)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kMaxCount;
  desc.max_mip_levels = 1;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->desc.mip_levels, 1u);
}

//===----------------------------------------------------------------------===//
// BC7 Encoding Tests (6.3.3)
//===----------------------------------------------------------------------===//

class TextureCookerBc7Test : public ::testing::Test { };

//! Test: CookTexture produces BC7 output when requested.
NOLINT_TEST_F(TextureCookerBc7Test, ProducesBc7Output)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.output_format = Format::kBC7UNorm;
  desc.bc7_quality = Bc7Quality::kFast;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(result.has_value())
    << "Error: " << static_cast<int>(result.error());

  EXPECT_EQ(result->desc.format, Format::kBC7UNorm);
  // BC7 block is 16 bytes for 4x4 pixels; 2x2 rounds up to 1 block
  EXPECT_GE(result->payload.size(), 16u);
}

//! Test: CookTexture fails with BC7 format but no BC7 quality.
NOLINT_TEST_F(TextureCookerBc7Test, FailsWithoutBc7Quality)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "test.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.output_format = Format::kBC7UNorm;
  desc.bc7_quality = Bc7Quality::kNone; // Invalid combination
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert - should fail validation
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kIntentFormatMismatch);
}

//===----------------------------------------------------------------------===//
// Normal Map Tests (6.3.6)
//===----------------------------------------------------------------------===//

class TextureCookerNormalMapTest : public ::testing::Test { };

//! Test: CookTexture with normal map intent produces valid output.
NOLINT_TEST_F(TextureCookerNormalMapTest, CooksNormalMap)
{
  // Arrange
  TextureImportDesc desc;
  desc.source_id = "normal.bmp";
  desc.width = 2;
  desc.height = 2;
  desc.intent = TextureIntent::kNormalTS;
  desc.source_color_space = ColorSpace::kLinear;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;
  desc.renormalize_normals_in_mips = true;

  // Act
  auto result
    = CookTexture(GetTestImageBytes(), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(result.has_value())
    << "Error: " << static_cast<int>(result.error());

  EXPECT_EQ(result->desc.format, Format::kRGBA8UNorm);
  EXPECT_FALSE(result->payload.empty());
}

//===----------------------------------------------------------------------===//
// Multi-Source Assembly Tests (6.3.9)
//===----------------------------------------------------------------------===//

class TextureCookerMultiSourceTest : public ::testing::Test { };

//! Test: CookTexture assembles array layers and mips from sources.
NOLINT_TEST_F(TextureCookerMultiSourceTest, AssemblesArrayLayersAndMips)
{
  // Arrange
  TextureSourceSet sources;
  sources.AddMipLevel(0, 0, MakeBmp2x2(), "layer0_mip0.bmp");
  sources.AddMipLevel(0, 1, MakeBmp1x1(), "layer0_mip1.bmp");
  sources.AddMipLevel(1, 0, MakeBmp2x2(), "layer1_mip0.bmp");
  sources.AddMipLevel(1, 1, MakeBmp1x1(), "layer1_mip1.bmp");

  TextureImportDesc desc;
  desc.source_id = "array_layers";
  desc.texture_type = TextureType::kTexture2DArray;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result = CookTexture(sources, desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(result.has_value())
    << "Error: " << static_cast<int>(result.error());
  EXPECT_EQ(result->desc.array_layers, 2u);
  EXPECT_EQ(result->desc.mip_levels, 2u);
  EXPECT_EQ(result->desc.format, Format::kRGBA8UNorm);
}

//! Test: CookTexture rejects mismatched dimensions across array layers.
NOLINT_TEST_F(
  TextureCookerMultiSourceTest, RejectsMismatchedDimensionsAcrossLayers)
{
  // Arrange
  TextureSourceSet sources;
  sources.AddMipLevel(0, 0, MakeBmp2x2(), "layer0_mip0.bmp");
  sources.AddMipLevel(1, 0, MakeBmp1x1(), "layer1_mip0.bmp");

  TextureImportDesc desc;
  desc.source_id = "array_layers_mismatch";
  desc.texture_type = TextureType::kTexture2D;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result = CookTexture(sources, desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kDimensionMismatch);
}

//! Test: CookTexture rejects missing mip levels in multi-source input.
NOLINT_TEST_F(TextureCookerMultiSourceTest, RejectsMissingMipLevel)
{
  // Arrange
  TextureSourceSet sources;
  sources.AddMipLevel(0, 0, MakeBmp2x2(), "layer0_mip0.bmp");
  sources.AddMipLevel(0, 1, MakeBmp1x1(), "layer0_mip1.bmp");
  sources.AddMipLevel(1, 0, MakeBmp2x2(), "layer1_mip0.bmp");

  TextureImportDesc desc;
  desc.source_id = "array_layers_missing_mip";
  desc.texture_type = TextureType::kTexture2D;
  desc.output_format = Format::kRGBA8UNorm;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result = CookTexture(sources, desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TextureImportError::kInvalidMipPolicy);
}

//===----------------------------------------------------------------------===//
// HDR Handling Tests (6.3.12)
//===----------------------------------------------------------------------===//

class TextureCookerHdrTest : public ::testing::Test { };

//! Test: CookTexture keeps float format when kKeepFloat is set.
NOLINT_TEST_F(TextureCookerHdrTest, KeepFloatOverridesLdrOutputFormat)
{
  // Arrange
  ScratchImage image = MakeFloatImage2x2();

  TextureImportDesc desc;
  desc.source_id = "hdr_float";
  desc.intent = TextureIntent::kHdrEnvironment;
  desc.hdr_handling = HdrHandling::kKeepFloat;
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.bake_hdr_to_ldr = false;
  desc.mip_policy = MipPolicy::kNone;

  // Act
  auto result
    = CookTexture(std::move(image), desc, TightPackedPolicy::Instance());

  // Assert
  ASSERT_TRUE(result.has_value())
    << "Error: " << static_cast<int>(result.error());
  EXPECT_EQ(result->desc.format, Format::kRGBA32Float);
}

//===----------------------------------------------------------------------===//
// Detail Function Tests
//===----------------------------------------------------------------------===//

class TextureCookerDetailTest : public ::testing::Test { };

//! Test: ComputeContentHash produces non-zero hash.
NOLINT_TEST_F(TextureCookerDetailTest, ContentHashNonZero)
{
  // Arrange
  std::vector<std::byte> data { std::byte { 1 }, std::byte { 2 },
    std::byte { 3 } };

  // Act
  auto hash = oxygen::content::import::detail::ComputeContentHash(data);

  // Assert
  EXPECT_NE(hash, 0u);
}

//! Test: ComputeContentHash produces different hashes for different data.
NOLINT_TEST_F(TextureCookerDetailTest, ContentHashVariesWithData)
{
  // Arrange
  std::vector<std::byte> data1 { std::byte { 1 }, std::byte { 2 },
    std::byte { 3 } };
  std::vector<std::byte> data2 { std::byte { 4 }, std::byte { 5 },
    std::byte { 6 } };

  // Act
  auto hash1 = oxygen::content::import::detail::ComputeContentHash(data1);
  auto hash2 = oxygen::content::import::detail::ComputeContentHash(data2);

  // Assert
  EXPECT_NE(hash1, hash2);
}

} // namespace
