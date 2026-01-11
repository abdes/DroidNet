//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/TextureCooker.h>
#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace {

using oxygen::ColorSpace;
using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::Bc7Quality;
using oxygen::content::import::CookedTexturePayload;
using oxygen::content::import::CookTexture;
using oxygen::content::import::D3D12PackingPolicy;
using oxygen::content::import::MipFilter;
using oxygen::content::import::MipPolicy;
using oxygen::content::import::TextureImportDesc;
using oxygen::content::import::TextureImportError;
using oxygen::content::import::TextureIntent;
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

//! Returns the test BMP image as a span of bytes.
[[nodiscard]] auto GetTestImageBytes() -> std::span<const std::byte>
{
  static const auto kTestBmp = MakeBmp2x2();
  return { kTestBmp.data(), kTestBmp.size() };
}

//===----------------------------------------------------------------------===//
// Validation Tests (6.2)
//===----------------------------------------------------------------------===//

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
  desc.texture_type = TextureType::kTexture2D;
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
  desc.texture_type = TextureType::kTexture2D;
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
