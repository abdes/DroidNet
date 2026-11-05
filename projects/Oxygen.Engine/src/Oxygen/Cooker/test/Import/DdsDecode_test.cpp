//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/DdsDecode.h>
#include <Oxygen/Cooker/Import/Internal/ImageDecode.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportError.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import {
namespace {

  constexpr std::uint32_t kFourCcFlag = 0x4U;
  constexpr std::uint32_t kRgbFlag = 0x40U;

  auto PutU32(std::vector<std::byte>& bytes, const std::size_t at,
    const std::uint32_t v) -> void
  {
    std::memcpy(std::span(bytes).subspan(at, sizeof(v)).data(), &v, sizeof(v));
  }

  [[nodiscard]] auto Code(const std::string_view text) -> std::uint32_t
  {
    auto value = std::uint32_t {};
    std::memcpy(&value, text.data(), sizeof(value));
    return value;
  }

  //! A DDS file's 128-byte header, independent of the decoder's constants.
  [[nodiscard]] auto Header(const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t pixel_flags,
    const std::uint32_t four_cc = 0U) -> std::vector<std::byte>
  {
    auto bytes = std::vector<std::byte>(128U);
    PutU32(bytes, 0U, Code("DDS "));
    PutU32(bytes, 4U, 124U);
    PutU32(bytes, 12U, height);
    PutU32(bytes, 16U, width);
    PutU32(bytes, 76U, 32U);
    PutU32(bytes, 80U, pixel_flags);
    PutU32(bytes, 84U, four_cc);
    return bytes;
  }

  auto Append(std::vector<std::byte>& bytes,
    const std::initializer_list<std::uint8_t> values) -> void
  {
    for (const auto value : values) {
      bytes.push_back(static_cast<std::byte>(value));
    }
  }

  [[nodiscard]] auto Pixel(const ScratchImage& image, const std::uint32_t x,
    const std::uint32_t y) -> std::array<std::uint8_t, 4>
  {
    const auto view = image.GetImage(0U, 0U);
    const auto at
      = (std::size_t { y } * view.row_pitch_bytes) + (std::size_t { x } * 4U);
    auto pixel = std::array<std::uint8_t, 4> {};
    std::memcpy(pixel.data(), view.pixels.subspan(at, 4U).data(), 4U);
    return pixel;
  }

  // Red 0xF800 and blue 0x001F as BC1 endpoints, with indices: row 0 all
  // endpoint 0, row 1 all endpoint 1, row 2 all 2/3-1/3, row 3 all 1/3-2/3.
  const auto kOpaqueBc1 = std::initializer_list<std::uint8_t> {
    0x00,
    0xF8,
    0x1F,
    0x00,
    0x00,
    0x55,
    0xAA,
    0xFF,
  };

  NOLINT_TEST(DdsDecodeTest, Bc1DecodesItsFourColors)
  {
    auto bytes = Header(4U, 4U, kFourCcFlag, Code("DXT1"));
    Append(bytes, kOpaqueBc1);
    const auto image = DecodeDdsToScratchImage(bytes, false);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->Meta().format, Format::kRGBA8UNorm);
    using P = std::array<std::uint8_t, 4>;
    EXPECT_EQ(Pixel(*image, 0U, 0U), (P { 255U, 0U, 0U, 255U }));
    EXPECT_EQ(Pixel(*image, 3U, 1U), (P { 0U, 0U, 255U, 255U }));
    EXPECT_EQ(Pixel(*image, 0U, 2U), (P { 170U, 0U, 85U, 255U }));
    EXPECT_EQ(Pixel(*image, 0U, 3U), (P { 85U, 0U, 170U, 255U }));
  }

  //! Endpoints in the other order mean three colors and transparent black.
  NOLINT_TEST(DdsDecodeTest, Bc1OrderedEndpointsCarryTransparency)
  {
    auto bytes = Header(4U, 4U, kFourCcFlag, Code("DXT1"));
    Append(bytes, { 0x1F, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xAA });
    const auto image = DecodeDdsToScratchImage(bytes, false);
    ASSERT_TRUE(image.has_value());
    using P = std::array<std::uint8_t, 4>;
    EXPECT_EQ(Pixel(*image, 0U, 0U), (P { 0U, 0U, 0U, 0U }));
    EXPECT_EQ(Pixel(*image, 0U, 3U), (P { 128U, 0U, 128U, 255U }));
  }

  NOLINT_TEST(DdsDecodeTest, Bc2AndBc3CarryAlpha)
  {
    auto bc2 = Header(4U, 4U, kFourCcFlag, Code("DXT3"));
    Append(bc2, { 0x4F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF });
    Append(bc2, kOpaqueBc1);
    const auto explicit_alpha = DecodeDdsToScratchImage(bc2, false);
    ASSERT_TRUE(explicit_alpha.has_value());
    EXPECT_EQ(Pixel(*explicit_alpha, 0U, 0U)[3], 255U);
    EXPECT_EQ(Pixel(*explicit_alpha, 1U, 0U)[3], 68U);

    // Alpha endpoints 200 and 100; row 0 reads index 0, then 1, 2 and 7.
    auto bc3 = Header(4U, 4U, kFourCcFlag, Code("DXT5"));
    Append(bc3, { 200U, 100U, 0x88, 0x0E, 0x00, 0x00, 0x00, 0x00 });
    Append(bc3, kOpaqueBc1);
    const auto interpolated = DecodeDdsToScratchImage(bc3, false);
    ASSERT_TRUE(interpolated.has_value());
    EXPECT_EQ(Pixel(*interpolated, 0U, 0U)[3], 200U);
    EXPECT_EQ(Pixel(*interpolated, 1U, 0U)[3], 100U);
    EXPECT_EQ(Pixel(*interpolated, 2U, 0U)[3], 186U);
    EXPECT_EQ(Pixel(*interpolated, 3U, 0U)[3], 114U);
    EXPECT_EQ(Pixel(*interpolated, 0U, 0U)[0], 255U);
  }

  NOLINT_TEST(DdsDecodeTest, Bc4ExpandsToGrey)
  {
    auto bytes = Header(4U, 4U, kFourCcFlag, Code("ATI1"));
    Append(bytes, { 90U, 200U, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    const auto image = DecodeDdsToScratchImage(bytes, false);
    ASSERT_TRUE(image.has_value());
    using P = std::array<std::uint8_t, 4>;
    EXPECT_EQ(Pixel(*image, 0U, 0U), (P { 90U, 90U, 90U, 255U }));
  }

  //! A flat normal (x and y at the middle) reads straight up, and a tilted one
  //! keeps unit length: glTF-style consumers read blue as z.
  NOLINT_TEST(DdsDecodeTest, Bc5ImpliesNormalZInBlue)
  {
    auto flat = Header(4U, 4U, kFourCcFlag, Code("ATI2"));
    Append(flat, { 128U, 0U, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    Append(flat, { 128U, 0U, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    const auto up = DecodeDdsToScratchImage(flat, false);
    ASSERT_TRUE(up.has_value());
    EXPECT_EQ(Pixel(*up, 0U, 0U)[0], 128U);
    EXPECT_EQ(Pixel(*up, 0U, 0U)[1], 128U);
    EXPECT_GE(Pixel(*up, 0U, 0U)[2], 254U);

    auto tilted = Header(4U, 4U, kFourCcFlag, Code("BC5U"));
    Append(tilted, { 255U, 0U, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    Append(tilted, { 128U, 0U, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    const auto side = DecodeDdsToScratchImage(tilted, false);
    ASSERT_TRUE(side.has_value());
    EXPECT_NEAR(static_cast<int>(Pixel(*side, 0U, 0U)[2]), 128, 1);
  }

  NOLINT_TEST(DdsDecodeTest, UncompressedMasksPickTheChannelOrder)
  {
    auto bytes = Header(1U, 1U, kRgbFlag);
    PutU32(bytes, 88U, 32U);
    PutU32(bytes, 92U, 0x00FF0000U);
    PutU32(bytes, 96U, 0x0000FF00U);
    PutU32(bytes, 100U, 0x000000FFU);
    PutU32(bytes, 104U, 0xFF000000U);
    Append(bytes, { 10U, 20U, 30U, 40U });
    const auto image = DecodeDdsToScratchImage(bytes, false);
    ASSERT_TRUE(image.has_value());
    using P = std::array<std::uint8_t, 4>;
    EXPECT_EQ(Pixel(*image, 0U, 0U), (P { 30U, 20U, 10U, 40U }));
  }

  //! Extents that are no multiple of four crop the last block.
  NOLINT_TEST(DdsDecodeTest, PartialBlocksCropAndFlipStaysInBounds)
  {
    auto bytes = Header(6U, 5U, kFourCcFlag, Code("DXT1"));
    for (int block = 0; block < 4; ++block) {
      Append(bytes, kOpaqueBc1);
    }
    const auto image = DecodeDdsToScratchImage(bytes, true);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->Meta().width, 6U);
    EXPECT_EQ(image->Meta().height, 5U);
    // Row 4 was the second block row's first, which reads endpoint 0.
    using P = std::array<std::uint8_t, 4>;
    EXPECT_EQ(Pixel(*image, 5U, 0U), (P { 255U, 0U, 0U, 255U }));
    // Row 3 of the first block reads 1/3-2/3 and lands on row 1 of the flip.
    EXPECT_EQ(Pixel(*image, 0U, 1U), (P { 85U, 0U, 170U, 255U }));
  }

  NOLINT_TEST(DdsDecodeTest, UnifiedDecoderFindsItBySignature)
  {
    auto bytes = Header(4U, 4U, kFourCcFlag, Code("DXT1"));
    Append(bytes, kOpaqueBc1);
    const auto image = DecodeToScratchImage(bytes);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->Meta().width, 4U);
  }

  NOLINT_TEST(DdsDecodeTest, RefusesWhatItCannotRead)
  {
    const auto error_of = [](const std::vector<std::byte>& bytes) {
      const auto image = DecodeDdsToScratchImage(bytes, false);
      EXPECT_FALSE(image.has_value());
      return image.has_value() ? TextureImportError::kDecodeFailed
                               : image.error();
    };
    auto short_data = Header(8U, 8U, kFourCcFlag, Code("DXT1"));
    Append(short_data, kOpaqueBc1);
    EXPECT_EQ(error_of(short_data), TextureImportError::kCorruptedData);

    auto empty_extent = Header(0U, 4U, kFourCcFlag, Code("DXT1"));
    Append(empty_extent, kOpaqueBc1);
    EXPECT_EQ(error_of(empty_extent), TextureImportError::kCorruptedData);

    const auto truncated = std::vector<std::byte>(16U, std::byte { 'D' });
    EXPECT_EQ(error_of(truncated), TextureImportError::kCorruptedData);

    auto unknown = Header(4U, 4U, kFourCcFlag, Code("RXGB"));
    Append(unknown, kOpaqueBc1);
    EXPECT_EQ(error_of(unknown), TextureImportError::kUnsupportedFormat);

    auto cube = Header(4U, 4U, kFourCcFlag, Code("DXT1"));
    PutU32(cube, 112U, 0x200U);
    Append(cube, kOpaqueBc1);
    EXPECT_EQ(error_of(cube), TextureImportError::kUnsupportedFormat);

    // BC7 under a DX10 header is not read.
    auto bc7 = Header(4U, 4U, kFourCcFlag, Code("DX10"));
    bc7.resize(148U);
    PutU32(bc7, 128U, 98U);
    PutU32(bc7, 132U, 3U);
    PutU32(bc7, 144U, 0U);
    EXPECT_EQ(error_of(bc7), TextureImportError::kUnsupportedFormat);
  }

} // namespace
} // namespace oxygen::content::import
