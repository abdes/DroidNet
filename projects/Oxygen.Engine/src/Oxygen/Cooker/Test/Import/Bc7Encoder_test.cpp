//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/bc7/Bc7Encoder.cpp

#include <array>
#include <cstddef>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/bc7/Bc7Encoder.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::content::import::Bc7Quality;
using oxygen::content::import::ImageView;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::ScratchImageMeta;
namespace bc7 = oxygen::content::import::bc7;

//===----------------------------------------------------------------------===//
// BC7 Encoder Parameters Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(Bc7EncoderParamsTest, FastHasExpectedValues)
{
  const auto params = bc7::Bc7EncoderParams::Fast();

  EXPECT_EQ(params.max_partitions, 16u);
  EXPECT_EQ(params.uber_level, 0u);
  EXPECT_FALSE(params.try_least_squares);
}

NOLINT_TEST(Bc7EncoderParamsTest, DefaultHasBalancedValues)
{
  const auto params = bc7::Bc7EncoderParams::Default();

  EXPECT_EQ(params.max_partitions, 64u);
  EXPECT_EQ(params.uber_level, 1u);
  EXPECT_TRUE(params.try_least_squares);
}

NOLINT_TEST(Bc7EncoderParamsTest, HighHasQualityValues)
{
  const auto params = bc7::Bc7EncoderParams::High();

  EXPECT_EQ(params.max_partitions, 64u);
  EXPECT_EQ(params.uber_level, 4u);
  EXPECT_TRUE(params.try_least_squares);
  EXPECT_FALSE(params.use_partition_filterbank);
}

NOLINT_TEST(Bc7EncoderParamsTest, FromQualityMapsCorrectly)
{
  EXPECT_EQ(
    bc7::Bc7EncoderParams::FromQuality(Bc7Quality::kFast).max_partitions,
    bc7::Bc7EncoderParams::Fast().max_partitions);
  EXPECT_EQ(bc7::Bc7EncoderParams::FromQuality(Bc7Quality::kDefault).uber_level,
    bc7::Bc7EncoderParams::Default().uber_level);
  EXPECT_EQ(bc7::Bc7EncoderParams::FromQuality(Bc7Quality::kHigh).uber_level,
    bc7::Bc7EncoderParams::High().uber_level);
}

//===----------------------------------------------------------------------===//
// BC7 Block Count Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(Bc7BlockCountTest, ComputeBlockCountExactMultiples)
{
  EXPECT_EQ(bc7::ComputeBlockCount(4), 1u);
  EXPECT_EQ(bc7::ComputeBlockCount(8), 2u);
  EXPECT_EQ(bc7::ComputeBlockCount(16), 4u);
  EXPECT_EQ(bc7::ComputeBlockCount(256), 64u);
}

NOLINT_TEST(Bc7BlockCountTest, ComputeBlockCountRoundsUp)
{
  EXPECT_EQ(bc7::ComputeBlockCount(1), 1u);
  EXPECT_EQ(bc7::ComputeBlockCount(2), 1u);
  EXPECT_EQ(bc7::ComputeBlockCount(3), 1u);
  EXPECT_EQ(bc7::ComputeBlockCount(5), 2u);
  EXPECT_EQ(bc7::ComputeBlockCount(7), 2u);
  EXPECT_EQ(bc7::ComputeBlockCount(9), 3u);
}

NOLINT_TEST(Bc7BlockCountTest, ComputeBc7RowPitchReturnsCorrectPitch)
{
  EXPECT_EQ(bc7::ComputeBc7RowPitch(4), 16u); // 1 block
  EXPECT_EQ(bc7::ComputeBc7RowPitch(8), 32u); // 2 blocks
  EXPECT_EQ(bc7::ComputeBc7RowPitch(16), 64u); // 4 blocks
  EXPECT_EQ(bc7::ComputeBc7RowPitch(5), 32u); // 2 blocks (rounded up)
}

NOLINT_TEST(Bc7BlockCountTest, ComputeBc7SurfaceSizeReturnsCorrectSize)
{
  EXPECT_EQ(bc7::ComputeBc7SurfaceSize(4, 4), 16u); // 1x1 blocks
  EXPECT_EQ(bc7::ComputeBc7SurfaceSize(8, 8), 64u); // 2x2 blocks
  EXPECT_EQ(bc7::ComputeBc7SurfaceSize(16, 16), 256u); // 4x4 blocks
  EXPECT_EQ(bc7::ComputeBc7SurfaceSize(5, 5), 64u); // 2x2 blocks (rounded)
}

//===----------------------------------------------------------------------===//
// BC7 Single Block Encoding Tests
//===----------------------------------------------------------------------===//

class Bc7EncodeBlockTest : public ::testing::Test {
protected:
  void SetUp() override { bc7::InitializeEncoder(); }
};

NOLINT_TEST_F(Bc7EncodeBlockTest, EncodeBlockProducesOutput)
{
  // Solid red 4x4 block
  std::array<std::byte, 64> pixels {};
  for (size_t i = 0; i < 16; ++i) {
    const size_t offset = i * 4;
    pixels[offset + 0] = std::byte { 255 }; // R
    pixels[offset + 1] = std::byte { 0 }; // G
    pixels[offset + 2] = std::byte { 0 }; // B
    pixels[offset + 3] = std::byte { 255 }; // A
  }

  std::array<std::byte, bc7::kBc7BlockSizeBytes> output {};
  const auto params = bc7::Bc7EncoderParams::Fast();

  const bool has_alpha = bc7::EncodeBlock(pixels, output, params);

  EXPECT_FALSE(has_alpha); // All alpha = 255

  // Check output is non-zero
  bool all_zero = true;
  for (const auto& byte : output) {
    if (byte != std::byte { 0 }) {
      all_zero = false;
      break;
    }
  }
  EXPECT_FALSE(all_zero);
}

NOLINT_TEST_F(Bc7EncodeBlockTest, EncodeBlockDetectsAlpha)
{
  // Block with partial transparency
  std::array<std::byte, 64> pixels {};
  for (size_t i = 0; i < 16; ++i) {
    const size_t offset = i * 4;
    pixels[offset + 0] = std::byte { 128 };
    pixels[offset + 1] = std::byte { 128 };
    pixels[offset + 2] = std::byte { 128 };
    pixels[offset + 3] = std::byte { 128 }; // 50% alpha
  }

  std::array<std::byte, bc7::kBc7BlockSizeBytes> output {};
  const auto params = bc7::Bc7EncoderParams::Fast();

  const bool has_alpha = bc7::EncodeBlock(pixels, output, params);

  EXPECT_TRUE(has_alpha);
}

//===----------------------------------------------------------------------===//
// BC7 Surface Encoding Tests
//===----------------------------------------------------------------------===//

class Bc7EncodeSurfaceTest : public ::testing::Test {
protected:
  void SetUp() override { bc7::InitializeEncoder(); }
};

NOLINT_TEST_F(Bc7EncodeSurfaceTest, EncodeSurface4x4ProducesValidOutput)
{
  // Create a 4x4 RGBA8 image
  std::vector<std::byte> pixels(4 * 4 * 4);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i + 0] = std::byte { 200 }; // R
    pixels[i + 1] = std::byte { 100 }; // G
    pixels[i + 2] = std::byte { 50 }; // B
    pixels[i + 3] = std::byte { 255 }; // A
  }

  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA8UNorm, 16, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  const auto source_view = source.GetImage(0, 0);
  const auto params = bc7::Bc7EncoderParams::Fast();

  auto result = bc7::EncodeSurface(source_view, params);

  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().width, 4u);
  EXPECT_EQ(result.Meta().height, 4u);
  EXPECT_EQ(result.Meta().format, Format::kBC7UNorm);
  EXPECT_EQ(result.GetTotalSizeBytes(), bc7::kBc7BlockSizeBytes);
}

NOLINT_TEST_F(Bc7EncodeSurfaceTest, EncodeSurfaceNonMultiple4HandlesEdges)
{
  // Create a 5x5 RGBA8 image
  std::vector<std::byte> pixels(5 * 5 * 4);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i + 0] = std::byte { 128 };
    pixels[i + 1] = std::byte { 128 };
    pixels[i + 2] = std::byte { 128 };
    pixels[i + 3] = std::byte { 255 };
  }

  auto source = ScratchImage::CreateFromData(
    5, 5, Format::kRGBA8UNorm, 20, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  const auto source_view = source.GetImage(0, 0);
  const auto params = bc7::Bc7EncoderParams::Fast();

  auto result = bc7::EncodeSurface(source_view, params);

  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().width, 5u);
  EXPECT_EQ(result.Meta().height, 5u);
  EXPECT_EQ(result.Meta().format, Format::kBC7UNorm);

  // 5x5 requires 2x2 blocks = 4 blocks * 16 bytes = 64 bytes
  EXPECT_EQ(result.GetTotalSizeBytes(), 64u);
}

NOLINT_TEST_F(Bc7EncodeSurfaceTest, EncodeSurfaceInvalidFormatReturnsEmpty)
{
  // Create a float image (wrong format)
  std::vector<std::byte> pixels(4 * 4 * 16); // RGBA32Float
  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA32Float, 64, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  const auto source_view = source.GetImage(0, 0);
  const auto params = bc7::Bc7EncoderParams::Fast();

  auto result = bc7::EncodeSurface(source_view, params);

  EXPECT_FALSE(result.IsValid());
}

//===----------------------------------------------------------------------===//
// BC7 Full Texture Encoding Tests
//===----------------------------------------------------------------------===//

class Bc7EncodeTextureTest : public ::testing::Test {
protected:
  void SetUp() override { bc7::InitializeEncoder(); }
};

NOLINT_TEST_F(Bc7EncodeTextureTest, EncodeTextureSingleMipSucceeds)
{
  std::vector<std::byte> pixels(8 * 8 * 4);
  for (auto& byte : pixels) {
    byte = std::byte { 128 };
  }

  auto source = ScratchImage::CreateFromData(
    8, 8, Format::kRGBA8UNorm, 32, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  auto result = bc7::EncodeTexture(source, bc7::Bc7EncoderParams::Fast());

  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().width, 8u);
  EXPECT_EQ(result.Meta().height, 8u);
  EXPECT_EQ(result.Meta().format, Format::kBC7UNorm);
  EXPECT_EQ(result.Meta().mip_levels, 1u);
}

NOLINT_TEST_F(Bc7EncodeTextureTest, EncodeTextureQualityPresetWorks)
{
  std::vector<std::byte> pixels(4 * 4 * 4);
  for (auto& byte : pixels) {
    byte = std::byte { 200 };
  }

  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA8UNorm, 16, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  auto result = bc7::EncodeTexture(source, Bc7Quality::kDefault);

  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().format, Format::kBC7UNorm);
}

NOLINT_TEST_F(Bc7EncodeTextureTest, EncodeTextureQualityNoneReturnsEmpty)
{
  std::vector<std::byte> pixels(4 * 4 * 4, std::byte { 128 });
  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA8UNorm, 16, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  auto result = bc7::EncodeTexture(source, Bc7Quality::kNone);

  EXPECT_FALSE(result.IsValid());
}

} // namespace
