//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/ImageDecode.h>

#include "FbxImporterTest.h"

namespace {

using oxygen::content::import::DecodeImageRgba8FromFile;
using oxygen::content::import::DecodeImageRgba8FromMemory;

class ImageDecodeTest : public oxygen::content::test::FbxImporterTest { };

[[nodiscard]] auto MakeBmp2x2() -> std::vector<std::byte>
{
  std::vector<std::byte> bytes;
  bytes.reserve(70);

  const auto push_u16 = [&](const uint16_t v) {
    bytes.push_back(std::byte { static_cast<uint8_t>(v & 0xFFu) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFu) });
  };
  const auto push_u32 = [&](const uint32_t v) {
    bytes.push_back(std::byte { static_cast<uint8_t>(v & 0xFFu) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFu) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFu) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFu) });
  };
  const auto push_i32
    = [&](const int32_t v) { push_u32(static_cast<uint32_t>(v)); };
  const auto push_bgra
    = [&](const uint8_t b, const uint8_t g, const uint8_t r, const uint8_t a) {
        bytes.push_back(std::byte { b });
        bytes.push_back(std::byte { g });
        bytes.push_back(std::byte { r });
        bytes.push_back(std::byte { a });
      };

  constexpr uint32_t kFileSize = 14u + 40u + 16u;
  constexpr uint32_t kDataOffset = 14u + 40u;

  // BITMAPFILEHEADER
  push_u16(0x4D42u);
  push_u32(kFileSize);
  push_u16(0u);
  push_u16(0u);
  push_u32(kDataOffset);

  // BITMAPINFOHEADER
  push_u32(40u);
  push_i32(2);
  push_i32(2);
  push_u16(1u);
  push_u16(32u);
  push_u32(0u);
  push_u32(16u);
  push_i32(0);
  push_i32(0);
  push_u32(0u);
  push_u32(0u);

  // Pixel data (BGRA), bottom-up rows.
  push_bgra(255u, 0u, 0u, 255u);
  push_bgra(255u, 255u, 255u, 255u);
  push_bgra(0u, 0u, 255u, 255u);
  push_bgra(0u, 255u, 0u, 255u);

  return bytes;
}

//! Test: DecodeImageRgba8FromMemory decodes a simple BMP.
/*!\
 Verifies image dimensions and RGBA8 output size.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeFromMemory_DecodesBmp)
{
  // Arrange
  const auto bmp = MakeBmp2x2();

  // Act
  const auto result = DecodeImageRgba8FromMemory(
    std::span<const std::byte>(bmp.data(), bmp.size()));

  // Assert
  ASSERT_TRUE(result.Succeeded());
  ASSERT_TRUE(result.image.has_value());
  EXPECT_EQ(result.image->width, 2u);
  EXPECT_EQ(result.image->height, 2u);
  EXPECT_EQ(result.image->pixels.size(), 16u);
}

//! Test: DecodeImageRgba8FromFile decodes a simple BMP from disk.
/*!\
 Verifies decode succeeds and the result is RGBA8.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeFromFile_DecodesBmp)
{
  // Arrange
  const auto temp_dir = MakeTempDir("image_decode_from_file");
  const auto path = temp_dir / "test.bmp";
  const auto bmp = MakeBmp2x2();
  WriteBinaryFile(path, std::span<const std::byte>(bmp.data(), bmp.size()));

  // Act
  const auto result = DecodeImageRgba8FromFile(path);

  // Assert
  ASSERT_TRUE(result.Succeeded());
  ASSERT_TRUE(result.image.has_value());
  EXPECT_EQ(result.image->width, 2u);
  EXPECT_EQ(result.image->height, 2u);
  EXPECT_EQ(result.image->pixels.size(), 16u);
}

//! Test: DecodeImageRgba8FromMemory fails on invalid input.
/*!\
 Verifies errors are reported for invalid image blobs.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeFromMemory_InvalidBytesFails)
{
  // Arrange
  const std::array<std::byte, 8> bytes
    = { std::byte { 0x00 }, std::byte { 0x01 }, std::byte { 0x02 },
        std::byte { 0x03 }, std::byte { 0x04 }, std::byte { 0x05 },
        std::byte { 0x06 }, std::byte { 0x07 } };

  // Act
  const auto result = DecodeImageRgba8FromMemory(
    std::span<const std::byte>(bytes.data(), bytes.size()));

  // Assert
  EXPECT_FALSE(result.Succeeded());
  EXPECT_FALSE(result.error.empty());
}

} // namespace
