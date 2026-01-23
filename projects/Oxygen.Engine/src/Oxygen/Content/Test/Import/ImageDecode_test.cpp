//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/Internal/ImageDecode.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Core/Types/Format.h>

namespace {

using oxygen::content::import::DecodeImageRgba8FromFile;
using oxygen::content::import::DecodeImageRgba8FromMemory;

class ImageDecodeTest : public ::testing::Test {
protected:
  [[nodiscard]] static auto MakeTempDir(std::string_view suffix)
    -> std::filesystem::path
  {
    const auto root
      = std::filesystem::temp_directory_path() / "oxgn-cntt-tests";
    const auto out_dir = root / std::filesystem::path(std::string(suffix));

    std::error_code ec;
    std::filesystem::remove_all(out_dir, ec);
    std::filesystem::create_directories(out_dir);

    return out_dir;
  }

  static auto WriteBinaryFile(const std::filesystem::path& path,
    const std::span<const std::byte> bytes) -> void
  {
    std::ofstream file(path.string(), std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  }
};

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

// ===========================================================================
// Phase 2: Format Detection Tests
// ===========================================================================

using oxygen::Format;
using oxygen::content::import::DecodeOptions;
using oxygen::content::import::DecodeToScratchImage;
using oxygen::content::import::IsExrSignature;
using oxygen::content::import::IsHdrFormat;
using oxygen::content::import::IsHdrSignature;

//! Test: IsExrSignature detects EXR magic bytes.
/*!\
 Verifies detection of OpenEXR magic number: 0x76 0x2F 0x31 0x01.
*/
NOLINT_TEST_F(ImageDecodeTest, IsExrSignature_DetectsValidMagic)
{
  // Arrange
  const std::array<std::byte, 8> exr_magic
    = { std::byte { 0x76 }, std::byte { 0x2F }, std::byte { 0x31 },
        std::byte { 0x01 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 } };

  // Act & Assert
  EXPECT_TRUE(IsExrSignature(exr_magic));
}

//! Test: IsExrSignature rejects non-EXR bytes.
/*!\
 Verifies that arbitrary bytes are not detected as EXR.
*/
NOLINT_TEST_F(ImageDecodeTest, IsExrSignature_RejectsNonExr)
{
  // Arrange
  const std::array<std::byte, 8> non_exr = { std::byte { 0x89 },
    std::byte { 'P' }, std::byte { 'N' }, std::byte { 'G' }, std::byte { 0x00 },
    std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 } };

  // Act & Assert
  EXPECT_FALSE(IsExrSignature(non_exr));
}

//! Test: IsExrSignature handles empty input.
/*!\
 Verifies graceful handling of empty byte spans.
*/
NOLINT_TEST_F(ImageDecodeTest, IsExrSignature_HandlesEmpty)
{
  // Arrange
  const std::span<const std::byte> empty;

  // Act & Assert
  EXPECT_FALSE(IsExrSignature(empty));
}

//! Test: IsHdrSignature detects Radiance HDR format.
/*!\
 Verifies detection of "#?RADIANCE" signature.
*/
NOLINT_TEST_F(ImageDecodeTest, IsHdrSignature_DetectsRadiance)
{
  // Arrange
  const std::string_view radiance_header = "#?RADIANCE\n";
  const auto* data = reinterpret_cast<const std::byte*>(radiance_header.data());
  const std::span<const std::byte> bytes(data, radiance_header.size());

  // Act & Assert
  EXPECT_TRUE(IsHdrSignature(bytes));
}

//! Test: IsHdrSignature detects RGBE format.
/*!\
 Verifies detection of "#?RGBE" signature.
*/
NOLINT_TEST_F(ImageDecodeTest, IsHdrSignature_DetectsRgbe)
{
  // Arrange
  const std::string_view rgbe_header = "#?RGBE\n";
  const auto* data = reinterpret_cast<const std::byte*>(rgbe_header.data());
  const std::span<const std::byte> bytes(data, rgbe_header.size());

  // Act & Assert
  EXPECT_TRUE(IsHdrSignature(bytes));
}

//! Test: IsHdrSignature rejects non-HDR data.
/*!\
 Verifies that arbitrary text is not detected as HDR.
*/
NOLINT_TEST_F(ImageDecodeTest, IsHdrSignature_RejectsNonHdr)
{
  // Arrange
  const std::string_view non_hdr = "Hello, World!";
  const auto* data = reinterpret_cast<const std::byte*>(non_hdr.data());
  const std::span<const std::byte> bytes(data, non_hdr.size());

  // Act & Assert
  EXPECT_FALSE(IsHdrSignature(bytes));
}

//! Test: IsHdrFormat uses extension hint for EXR.
/*!\
 Verifies .exr extension is recognized as HDR format.
*/
NOLINT_TEST_F(ImageDecodeTest, IsHdrFormat_RecognizesExrExtension)
{
  // Arrange
  const std::array<std::byte, 4> random_data = { std::byte { 0x00 },
    std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x03 } };

  // Act & Assert
  EXPECT_TRUE(IsHdrFormat(random_data, ".exr"));
  EXPECT_TRUE(IsHdrFormat(random_data, ".EXR"));
}

//! Test: IsHdrFormat uses extension hint for HDR.
/*!\
 Verifies .hdr extension is recognized as HDR format.
*/
NOLINT_TEST_F(ImageDecodeTest, IsHdrFormat_RecognizesHdrExtension)
{
  // Arrange
  const std::array<std::byte, 4> random_data = { std::byte { 0x00 },
    std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x03 } };

  // Act & Assert
  EXPECT_TRUE(IsHdrFormat(random_data, ".hdr"));
  EXPECT_TRUE(IsHdrFormat(random_data, ".HDR"));
}

// ===========================================================================
// Phase 2: Unified Decode API Tests
// ===========================================================================

//! Test: DecodeToScratchImage decodes LDR BMP to RGBA8.
/*!\
 Verifies unified decode produces ScratchImage with RGBA8UNorm for LDR.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeToScratchImage_LdrBmpProducesRgba8)
{
  // Arrange
  const auto bmp = MakeBmp2x2();
  DecodeOptions options {};
  options.force_rgba = true;

  // Act
  auto result = DecodeToScratchImage(bmp, options);

  // Assert
  ASSERT_TRUE(result.has_value())
    << "Decode failed with error: " << static_cast<int>(result.error());
  EXPECT_EQ(result->Meta().width, 2u);
  EXPECT_EQ(result->Meta().height, 2u);
  EXPECT_EQ(result->Meta().format, Format::kRGBA8UNorm);
}

//! Test: DecodeToScratchImage applies Y-flip correctly.
/*!\
 Verifies flip_y option inverts the image vertically.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeToScratchImage_FlipsY)
{
  // Arrange
  const auto bmp = MakeBmp2x2();
  DecodeOptions options {};
  options.flip_y = true;
  options.force_rgba = true;

  // Act
  auto normal_result
    = DecodeToScratchImage(bmp, DecodeOptions { .force_rgba = true });
  auto flipped_result = DecodeToScratchImage(bmp, options);

  // Assert
  ASSERT_TRUE(normal_result.has_value());
  ASSERT_TRUE(flipped_result.has_value());

  // Get top-left pixel from both images
  auto normal_view = normal_result->GetImage(0, 0);
  auto flipped_view = flipped_result->GetImage(0, 0);

  // Top row of normal should equal bottom row of flipped
  const auto* normal_top = normal_view.pixels.data();
  const auto row_pitch = normal_view.row_pitch_bytes;
  const auto* flipped_bottom = flipped_view.pixels.data()
    + static_cast<size_t>(normal_result->Meta().height - 1) * row_pitch;

  EXPECT_EQ(std::memcmp(normal_top, flipped_bottom, row_pitch), 0);
}

//! Test: DecodeToScratchImage fails gracefully on empty input.
/*!\
 Verifies error handling for empty byte spans.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeToScratchImage_EmptyInputFails)
{
  // Arrange
  const std::span<const std::byte> empty;
  DecodeOptions options {};

  // Act
  auto result = DecodeToScratchImage(empty, options);

  // Assert
  EXPECT_FALSE(result.has_value());
}

//! Test: DecodeToScratchImage fails gracefully on corrupt data.
/*!\
 Verifies error handling for invalid image data.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeToScratchImage_CorruptDataFails)
{
  // Arrange
  const std::array<std::byte, 8> garbage
    = { std::byte { 0x12 }, std::byte { 0x34 }, std::byte { 0x56 },
        std::byte { 0x78 }, std::byte { 0x9A }, std::byte { 0xBC },
        std::byte { 0xDE }, std::byte { 0xF0 } };
  DecodeOptions options {};

  // Act
  auto result = DecodeToScratchImage(garbage, options);

  // Assert
  EXPECT_FALSE(result.has_value());
}

//! Test: DecodeToScratchImage from file works with LDR BMP.
/*!\
 Verifies file-based decode produces valid ScratchImage.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeToScratchImage_FromFile_LdrBmp)
{
  // Arrange
  const auto temp_dir = MakeTempDir("decode_to_scratch_file");
  const auto path = temp_dir / "test.bmp";
  const auto bmp = MakeBmp2x2();
  WriteBinaryFile(path, std::span<const std::byte>(bmp.data(), bmp.size()));
  DecodeOptions options { .force_rgba = true };

  // Act
  auto result = DecodeToScratchImage(path, options);

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->Meta().width, 2u);
  EXPECT_EQ(result->Meta().height, 2u);
  EXPECT_EQ(result->Meta().format, Format::kRGBA8UNorm);
}

//! Test: DecodeToScratchImage from file fails for non-existent file.
/*!\
 Verifies file not found error is returned.
*/
NOLINT_TEST_F(ImageDecodeTest, DecodeToScratchImage_FromFile_NotFoundFails)
{
  // Arrange
  const std::filesystem::path non_existent = "/non/existent/file.bmp";
  DecodeOptions options {};

  // Act
  auto result = DecodeToScratchImage(non_existent, options);

  // Assert
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(
    result.error(), oxygen::content::import::TextureImportError::kFileNotFound);
}

} // namespace
