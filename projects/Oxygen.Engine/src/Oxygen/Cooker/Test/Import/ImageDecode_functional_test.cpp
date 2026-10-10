//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImageDecode.cpp

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImageDecode.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::content::import::DecodeImageRgba8FromFile;
using oxygen::content::import::DecodeOptions;
using oxygen::content::import::DecodeToScratchImage;

class ImageDecodeTest : public oxygen::cooker::test::TempDirTest { };

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

NOLINT_TEST_F(ImageDecodeTest, DecodeFromFileDecodesBmp)
{
  const auto path = TempPath("test.bmp");
  const auto bmp = MakeBmp2x2();
  oxygen::cooker::test::WriteBytes(path, bmp);

  const auto result = DecodeImageRgba8FromFile(path);

  ASSERT_TRUE(result.Succeeded());
  ASSERT_TRUE(result.image.has_value());
  EXPECT_EQ(result.image->width, 2u);
  EXPECT_EQ(result.image->height, 2u);
  EXPECT_EQ(result.image->pixels.size(), 16u);
}

NOLINT_TEST_F(ImageDecodeTest, DecodeToScratchImageFromFileLdrBmp)
{
  const auto path = TempPath("test.bmp");
  const auto bmp = MakeBmp2x2();
  oxygen::cooker::test::WriteBytes(path, bmp);
  DecodeOptions options { .force_rgba = true };

  auto result = DecodeToScratchImage(path, options);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->Meta().width, 2u);
  EXPECT_EQ(result->Meta().height, 2u);
  EXPECT_EQ(result->Meta().format, Format::kRGBA8UNorm);
}

} // namespace
