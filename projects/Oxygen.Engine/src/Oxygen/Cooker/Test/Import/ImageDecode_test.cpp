//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImageDecode.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImageDecode.h>
#include <Oxygen/Cooker/Import/TextureImportError.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::DecodeImageRgba8FromMemory;

[[nodiscard]] auto MakeBmp2x2() -> std::vector<std::byte>
{
  std::vector<std::byte> bytes;
  bytes.reserve(70);

  const auto push_u16 = [&](const uint16_t v) -> void {
    bytes.push_back(std::byte { static_cast<uint8_t>(v & 0xFFU) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
  };
  const auto push_u32 = [&](const uint32_t v) -> void {
    bytes.push_back(std::byte { static_cast<uint8_t>(v & 0xFFU) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
  };
  const auto push_i32
    = [&](const int32_t v) -> void { push_u32(static_cast<uint32_t>(v)); };
  const auto push_bgra = [&](const uint8_t b, const uint8_t g, const uint8_t r,
                           const uint8_t a) -> void {
    bytes.push_back(std::byte { b });
    bytes.push_back(std::byte { g });
    bytes.push_back(std::byte { r });
    bytes.push_back(std::byte { a });
  };

  constexpr uint32_t kFileSize = 14U + 40U + 16U;
  constexpr uint32_t kDataOffset = 14U + 40U;

  // BITMAPFILEHEADER
  push_u16(0x4D42U);
  push_u32(kFileSize);
  push_u16(0U);
  push_u16(0U);
  push_u32(kDataOffset);

  // BITMAPINFOHEADER
  push_u32(40U);
  push_i32(2);
  push_i32(2);
  push_u16(1U);
  push_u16(32U);
  push_u32(0U);
  push_u32(16U);
  push_i32(0);
  push_i32(0);
  push_u32(0U);
  push_u32(0U);

  // Pixel data (BGRA), bottom-up rows.
  push_bgra(255U, 0U, 0U, 255U);
  push_bgra(255U, 255U, 255U, 255U);
  push_bgra(0U, 0U, 255U, 255U);
  push_bgra(0U, 255U, 0U, 255U);

  return bytes;
}

NOLINT_TEST(ImageDecodeTest, DecodeFromMemoryDecodesBmp)
{
  const auto bmp = MakeBmp2x2();

  const auto result = DecodeImageRgba8FromMemory(
    std::span<const std::byte>(bmp.data(), bmp.size()));

  ASSERT_TRUE(result.Succeeded());
  ASSERT_HAS_VALUE(result.image);
  EXPECT_EQ(result.image->width, 2U);
  EXPECT_EQ(result.image->height, 2U);
  EXPECT_EQ(result.image->pixels.size(), 16U);
}

NOLINT_TEST(ImageDecodeTest, DecodeFromMemoryInvalidBytesFails)
{
  const std::array<std::byte, 8> bytes = {
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x02 },
    std::byte { 0x03 },
    std::byte { 0x04 },
    std::byte { 0x05 },
    std::byte { 0x06 },
    std::byte { 0x07 },
  };

  const auto result = DecodeImageRgba8FromMemory(
    std::span<const std::byte>(bytes.data(), bytes.size()));

  EXPECT_FALSE(result.Succeeded());
  EXPECT_FALSE(result.error.empty());
}

// ===========================================================================
// Phase 2: Format Detection Tests
// ===========================================================================

using oxygen::Format;
using oxygen::content::import::DecodeOptions;
using oxygen::content::import::DecodeToScratchImage;
using oxygen::content::import::IsExrSignature;
using oxygen::content::import::IsHdrFormat;
using oxygen::content::import::IsHdrSignature;

NOLINT_TEST(ImageDecodeTest, IsExrSignatureDetectsValidMagic)
{
  const std::array<std::byte, 8> exr_magic = {
    std::byte { 0x76 },
    std::byte { 0x2F },
    std::byte { 0x31 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
  };

  EXPECT_TRUE(IsExrSignature(exr_magic));
}

NOLINT_TEST(ImageDecodeTest, IsExrSignatureRejectsNonExr)
{
  const std::array<std::byte, 8> non_exr = {
    std::byte { 0x89 },
    std::byte { 'P' },
    std::byte { 'N' },
    std::byte { 'G' },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
  };

  EXPECT_FALSE(IsExrSignature(non_exr));
}

NOLINT_TEST(ImageDecodeTest, IsExrSignatureHandlesEmpty)
{
  const std::span<const std::byte> empty;

  EXPECT_FALSE(IsExrSignature(empty));
}

NOLINT_TEST(ImageDecodeTest, IsHdrSignatureDetectsRadiance)
{
  const std::string_view radiance_header = "#?RADIANCE\n";
  const auto* data = reinterpret_cast<const std::byte*>(radiance_header.data());
  const std::span<const std::byte> bytes(data, radiance_header.size());

  EXPECT_TRUE(IsHdrSignature(bytes));
}

NOLINT_TEST(ImageDecodeTest, IsHdrSignatureDetectsRgbe)
{
  const std::string_view rgbe_header = "#?RGBE\n";
  const auto* data = reinterpret_cast<const std::byte*>(rgbe_header.data());
  const std::span<const std::byte> bytes(data, rgbe_header.size());

  EXPECT_TRUE(IsHdrSignature(bytes));
}

NOLINT_TEST(ImageDecodeTest, IsHdrSignatureRejectsNonHdr)
{
  const std::string_view non_hdr = "Hello, World!";
  const auto* data = reinterpret_cast<const std::byte*>(non_hdr.data());
  const std::span<const std::byte> bytes(data, non_hdr.size());

  EXPECT_FALSE(IsHdrSignature(bytes));
}

NOLINT_TEST(ImageDecodeTest, IsHdrFormatRecognizesExrExtension)
{
  const std::array<std::byte, 4> random_data = {
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x02 },
    std::byte { 0x03 },
  };

  EXPECT_TRUE(IsHdrFormat(random_data, ".exr"));
  EXPECT_TRUE(IsHdrFormat(random_data, ".EXR"));
}

NOLINT_TEST(ImageDecodeTest, IsHdrFormatRecognizesHdrExtension)
{
  const std::array<std::byte, 4> random_data = {
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x02 },
    std::byte { 0x03 },
  };

  EXPECT_TRUE(IsHdrFormat(random_data, ".hdr"));
  EXPECT_TRUE(IsHdrFormat(random_data, ".HDR"));
}

// ===========================================================================
// Phase 2: Unified Decode API Tests
// ===========================================================================

NOLINT_TEST(ImageDecodeTest, DecodeToScratchImageLdrBmpProducesRgba8)
{
  const auto bmp = MakeBmp2x2();
  DecodeOptions options {};
  options.force_rgba = true;

  auto result = DecodeToScratchImage(bmp, options);

  ASSERT_HAS_VALUE(result) << "Decode failed with error: "
                           << static_cast<int>(result.error());
  EXPECT_EQ(result->Meta().width, 2U);
  EXPECT_EQ(result->Meta().height, 2U);
  EXPECT_EQ(result->Meta().format, Format::kRGBA8UNorm);
}

NOLINT_TEST(ImageDecodeTest, DecodeToScratchImageFlipsY)
{
  const auto bmp = MakeBmp2x2();
  DecodeOptions options {};
  options.flip_y = true;
  options.force_rgba = true;

  auto normal_result
    = DecodeToScratchImage(bmp, DecodeOptions { .force_rgba = true });
  auto flipped_result = DecodeToScratchImage(bmp, options);

  ASSERT_HAS_VALUE(normal_result);
  ASSERT_HAS_VALUE(flipped_result);

  // Get top-left pixel from both images
  auto normal_view = normal_result->GetImage(0, 0);
  auto flipped_view = flipped_result->GetImage(0, 0);

  // Top row of normal should equal bottom row of flipped
  const auto* normal_top = normal_view.pixels.data();
  const auto row_pitch = normal_view.row_pitch_bytes;
  const auto* flipped_bottom = flipped_view.pixels.data()
    + (static_cast<size_t>(normal_result->Meta().height - 1) * row_pitch);

  EXPECT_EQ(std::memcmp(normal_top, flipped_bottom, row_pitch), 0);
}

NOLINT_TEST(ImageDecodeTest, DecodeToScratchImageEmptyInputFails)
{
  const std::span<const std::byte> empty;
  DecodeOptions options {};

  auto result = DecodeToScratchImage(empty, options);

  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST(ImageDecodeTest, DecodeToScratchImageCorruptDataFails)
{
  const std::array<std::byte, 8> garbage = {
    std::byte { 0x12 },
    std::byte { 0x34 },
    std::byte { 0x56 },
    std::byte { 0x78 },
    std::byte { 0x9A },
    std::byte { 0xBC },
    std::byte { 0xDE },
    std::byte { 0xF0 },
  };
  DecodeOptions options {};

  auto result = DecodeToScratchImage(garbage, options);

  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST(ImageDecodeTest, DecodeToScratchImageFromFileNotFoundFails)
{
  const std::filesystem::path non_existent = "/non/existent/file.bmp";
  DecodeOptions options {};

  auto result = DecodeToScratchImage(non_existent, options);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(
    result.error(), oxygen::content::import::TextureImportError::kFileNotFound);
}

} // namespace
