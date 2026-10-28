//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImageDecode.cpp (WebP and KTX2 decoders)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <span>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImageDecode.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportError.h>
#include <Oxygen/Cooker/test/Support/TestImages.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::content::import::DecodeOptions;
using oxygen::content::import::DecodeToScratchImage;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::TextureImportError;
using oxygen::cooker::test::Ktx2Encoding;
using oxygen::cooker::test::MakeKtx2;
using oxygen::cooker::test::MakeWebp;

constexpr auto kSize = uint32_t { 8U };
constexpr auto kBytesPerPixel = std::size_t { 4U };
constexpr auto kRowBytes = std::size_t { kSize } * kBytesPerPixel;

//! An 8x8 image of four solid 4x4 quadrants (red, green, blue, white over
//! half-transparent grey), so every Basis block is one colour.
[[nodiscard]] auto Quadrants() -> std::vector<std::byte>
{
  constexpr auto kHalf = kSize / 2U;
  constexpr auto kColors = std::to_array<std::array<uint8_t, 4>>({
    { 255U, 0U, 0U, 255U },
    { 0U, 255U, 0U, 255U },
    { 0U, 0U, 255U, 255U },
    { 128U, 128U, 128U, 128U },
  });
  auto pixels = std::vector<std::byte> {};
  pixels.reserve(kRowBytes * kSize);
  for (const auto y : std::views::iota(0U, kSize)) {
    for (const auto x : std::views::iota(0U, kSize)) {
      const auto quadrant = ((y / kHalf) * 2U) + (x / kHalf);
      for (const auto channel : kColors.at(quadrant)) {
        pixels.push_back(std::byte { channel });
      }
    }
  }
  return pixels;
}

//! `pixels` with its rows in reverse order.
[[nodiscard]] auto Flipped(std::span<const std::byte> pixels)
  -> std::vector<std::byte>
{
  auto rows = std::vector<std::byte> {};
  rows.reserve(pixels.size());
  for (const auto row : std::views::reverse(std::views::iota(0U, kSize))) {
    const auto source = pixels.subspan(row * kRowBytes, kRowBytes);
    rows.insert(rows.end(), source.begin(), source.end());
  }
  return rows;
}

//! The largest difference of one channel between the image and `expected`.
[[nodiscard]] auto MaxChannelError(
  const ScratchImage& image, std::span<const std::byte> expected) -> int
{
  const auto view = image.GetImage(0, 0);
  auto error = 0;
  for (const auto row : std::views::iota(0U, kSize)) {
    const auto actual = view.pixels.subspan(
      std::size_t { row } * view.row_pitch_bytes, kRowBytes);
    const auto wanted = expected.subspan(row * kRowBytes, kRowBytes);
    for (const auto [a, b] : std::views::zip(actual, wanted)) {
      error = std::max(
        error, std::abs(std::to_integer<int>(a) - std::to_integer<int>(b)));
    }
  }
  return error;
}

auto ExpectRgba8(const ScratchImage& image) -> void
{
  EXPECT_EQ(image.Meta().width, kSize);
  EXPECT_EQ(image.Meta().height, kSize);
  EXPECT_EQ(image.Meta().format, Format::kRGBA8UNorm);
}

//=== WebP ===----------------------------------------------------------------//

NOLINT_TEST(CompressedImageDecodeTest, LosslessWebpDecodesToItsPixels)
{
  const auto pixels = Quadrants();

  const auto decoded = DecodeToScratchImage(MakeWebp(kSize, kSize, pixels));

  ASSERT_HAS_VALUE(decoded);
  ExpectRgba8(*decoded);
  EXPECT_EQ(MaxChannelError(*decoded, pixels), 0);
}

NOLINT_TEST(CompressedImageDecodeTest, WebpFlipsYOnRequest)
{
  const auto pixels = Quadrants();

  const auto decoded = DecodeToScratchImage(
    MakeWebp(kSize, kSize, pixels), DecodeOptions { .flip_y = true });

  ASSERT_HAS_VALUE(decoded);
  EXPECT_EQ(MaxChannelError(*decoded, Flipped(pixels)), 0);
}

NOLINT_TEST(CompressedImageDecodeTest, AnimatedWebpIsRefused)
{
  // RIFF header and a VP8X chunk whose flags announce an animation.
  constexpr auto kAnimated = std::to_array<uint8_t>({
    'R',
    'I',
    'F',
    'F',
    22U,
    0U,
    0U,
    0U,
    'W',
    'E',
    'B',
    'P', //
    'V',
    'P',
    '8',
    'X',
    10U,
    0U,
    0U,
    0U, //
    0x02U,
    0U,
    0U,
    0U, // flags: animation
    7U,
    0U,
    0U,
    7U,
    0U,
    0U, // canvas 8x8, stored minus one
  });

  const auto decoded
    = DecodeToScratchImage(std::as_bytes(std::span(kAnimated)));

  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), TextureImportError::kUnsupportedFormat);
}

NOLINT_TEST(CompressedImageDecodeTest, TruncatedWebpIsCorrupt)
{
  const auto webp = MakeWebp(kSize, kSize, Quadrants());
  constexpr auto kHeaderOnly = std::size_t { 16 };
  ASSERT_GT(webp.size(), kHeaderOnly);

  const auto decoded = DecodeToScratchImage(std::span(webp).first(kHeaderOnly));

  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), TextureImportError::kCorruptedData);
}

//=== KTX2 ===----------------------------------------------------------------//

struct BasisCase final {
  Ktx2Encoding encoding;
  //! The largest channel error the encoding keeps on solid 4x4 blocks.
  int tolerance;
};

class BasisTranscodeTest : public ::testing::TestWithParam<BasisCase> { };

NOLINT_TEST_P(BasisTranscodeTest, TranscodesToItsPixels)
{
  const auto pixels = Quadrants();
  const auto ktx2 = MakeKtx2({
    .width = kSize,
    .height = kSize,
    .rgba = pixels,
    .encoding = GetParam().encoding,
  });

  const auto decoded = DecodeToScratchImage(ktx2);

  ASSERT_HAS_VALUE(decoded);
  ExpectRgba8(*decoded);
  EXPECT_LE(MaxChannelError(*decoded, pixels), GetParam().tolerance);
}

NOLINT_TEST_P(BasisTranscodeTest, BottomUpTextureDecodesTopRowFirst)
{
  const auto pixels = Quadrants();
  const auto ktx2 = MakeKtx2({
    .width = kSize,
    .height = kSize,
    .rgba = pixels,
    .encoding = GetParam().encoding,
    .bottom_up = true,
  });

  const auto decoded = DecodeToScratchImage(ktx2);
  const auto flipped
    = DecodeToScratchImage(ktx2, DecodeOptions { .flip_y = true });

  ASSERT_HAS_VALUE(decoded);
  ASSERT_HAS_VALUE(flipped);
  EXPECT_LE(MaxChannelError(*decoded, pixels), GetParam().tolerance);
  EXPECT_LE(MaxChannelError(*flipped, Flipped(pixels)), GetParam().tolerance);
}

INSTANTIATE_TEST_SUITE_P(Encodings, BasisTranscodeTest,
  ::testing::Values(
    BasisCase { .encoding = Ktx2Encoding::kEtc1s, .tolerance = 16 },
    BasisCase { .encoding = Ktx2Encoding::kUastc, .tolerance = 4 }));

NOLINT_TEST(CompressedImageDecodeTest, UncompressedKtx2IsRefused)
{
  const auto pixels = Quadrants();
  const auto ktx2 = MakeKtx2({
    .width = kSize,
    .height = kSize,
    .rgba = pixels,
    .encoding = Ktx2Encoding::kRaw,
  });

  const auto decoded = DecodeToScratchImage(ktx2);

  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), TextureImportError::kUnsupportedFormat);
}

NOLINT_TEST(CompressedImageDecodeTest, CubeMapKtx2IsRefused)
{
  const auto pixels = Quadrants();
  const auto ktx2 = MakeKtx2({
    .width = kSize,
    .height = kSize,
    .rgba = pixels,
    .encoding = Ktx2Encoding::kRaw,
    .faces = 6U,
  });

  const auto decoded = DecodeToScratchImage(ktx2);

  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), TextureImportError::kArrayLayerCountInvalid);
}

NOLINT_TEST(CompressedImageDecodeTest, TruncatedKtx2IsCorrupt)
{
  const auto pixels = Quadrants();
  const auto ktx2 = MakeKtx2({
    .width = kSize,
    .height = kSize,
    .rgba = pixels,
  });
  constexpr auto kIdentifierAndPart = std::size_t { 20 };
  ASSERT_GT(ktx2.size(), kIdentifierAndPart);

  const auto decoded
    = DecodeToScratchImage(std::span(ktx2).first(kIdentifierAndPart));

  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), TextureImportError::kCorruptedData);
}

} // namespace
