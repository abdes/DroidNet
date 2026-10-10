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

NOLINT_TEST_F(ImageDecodeTest, DecodeFromFileDecodesBmp)
{
  const auto path = TempPath("test.bmp");
  const auto bmp = MakeBmp2x2();
  oxygen::cooker::test::WriteBytes(path, bmp);

  const auto result = DecodeImageRgba8FromFile(path);

  ASSERT_TRUE(result.Succeeded());
  ASSERT_HAS_VALUE(result.image);
  EXPECT_EQ(result.image->width, 2U);
  EXPECT_EQ(result.image->height, 2U);
  EXPECT_EQ(result.image->pixels.size(), 16U);
}

NOLINT_TEST_F(ImageDecodeTest, DecodeToScratchImageFromFileLdrBmp)
{
  const auto path = TempPath("test.bmp");
  const auto bmp = MakeBmp2x2();
  oxygen::cooker::test::WriteBytes(path, bmp);
  DecodeOptions options { .force_rgba = true };

  auto result = DecodeToScratchImage(path, options);

  ASSERT_HAS_VALUE(result);
  EXPECT_EQ(result->Meta().width, 2U);
  EXPECT_EQ(result->Meta().height, 2U);
  EXPECT_EQ(result->Meta().format, Format::kRGBA8UNorm);
}

} // namespace
