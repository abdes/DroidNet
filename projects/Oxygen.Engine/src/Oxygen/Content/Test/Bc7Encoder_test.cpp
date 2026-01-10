//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/bc7/Bc7Encoder.h>

namespace {

using oxygen::Format;
using oxygen::content::import::Bc7Quality;
using oxygen::content::import::ImageView;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::ScratchImageMeta;
namespace bc7 = oxygen::content::import::bc7;

//===----------------------------------------------------------------------===//
// BC7 Encoder Parameters Tests (4.1)
//===----------------------------------------------------------------------===//

class Bc7EncoderParamsTest : public ::testing::Test { };

//! Test: Fast preset has expected values.
/*!\
 Verifies fast encoding parameters are configured for speed.
*/
NOLINT_TEST_F(Bc7EncoderParamsTest, Fast_HasExpectedValues)
{
  // Arrange & Act
  const auto params = bc7::Bc7EncoderParams::Fast();

  // Assert
  EXPECT_EQ(params.max_partitions, 16u);
  EXPECT_EQ(params.uber_level, 0u);
  EXPECT_FALSE(params.try_least_squares);
}

//! Test: Default preset has balanced values.
/*!\
 Verifies default encoding parameters balance quality and speed.
*/
NOLINT_TEST_F(Bc7EncoderParamsTest, Default_HasBalancedValues)
{
  // Arrange & Act
  const auto params = bc7::Bc7EncoderParams::Default();

  // Assert
  EXPECT_EQ(params.max_partitions, 64u);
  EXPECT_EQ(params.uber_level, 1u);
  EXPECT_TRUE(params.try_least_squares);
}

//! Test: High preset has quality-focused values.
/*!\
 Verifies high quality parameters maximize quality.
*/
NOLINT_TEST_F(Bc7EncoderParamsTest, High_HasQualityValues)
{
  // Arrange & Act
  const auto params = bc7::Bc7EncoderParams::High();

  // Assert
  EXPECT_EQ(params.max_partitions, 64u);
  EXPECT_EQ(params.uber_level, 4u);
  EXPECT_TRUE(params.try_least_squares);
  EXPECT_FALSE(params.use_partition_filterbank);
}

//! Test: FromQuality maps quality tiers correctly.
/*!\
 Verifies Bc7Quality enum maps to correct parameters.
*/
NOLINT_TEST_F(Bc7EncoderParamsTest, FromQuality_MapsCorrectly)
{
  // Arrange & Act & Assert
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

class Bc7BlockCountTest : public ::testing::Test { };

//! Test: ComputeBlockCount handles exact multiples of 4.
/*!\
 Verifies block count for dimensions divisible by 4.
*/
NOLINT_TEST_F(Bc7BlockCountTest, ComputeBlockCount_ExactMultiples)
{
  // Arrange & Act & Assert
  EXPECT_EQ(bc7::ComputeBlockCount(4), 1u);
  EXPECT_EQ(bc7::ComputeBlockCount(8), 2u);
  EXPECT_EQ(bc7::ComputeBlockCount(16), 4u);
  EXPECT_EQ(bc7::ComputeBlockCount(256), 64u);
}

//! Test: ComputeBlockCount rounds up for non-multiples.
/*!\
 Verifies block count rounds up for dimensions not divisible by 4.
*/
NOLINT_TEST_F(Bc7BlockCountTest, ComputeBlockCount_RoundsUp)
{
  // Arrange & Act & Assert
  EXPECT_EQ(bc7::ComputeBlockCount(1), 1u);
  EXPECT_EQ(bc7::ComputeBlockCount(2), 1u);
  EXPECT_EQ(bc7::ComputeBlockCount(3), 1u);
  EXPECT_EQ(bc7::ComputeBlockCount(5), 2u);
  EXPECT_EQ(bc7::ComputeBlockCount(7), 2u);
  EXPECT_EQ(bc7::ComputeBlockCount(9), 3u);
}

//! Test: ComputeBc7RowPitch returns correct pitch.
/*!\
 Verifies row pitch is blocks_x * 16 bytes.
*/
NOLINT_TEST_F(Bc7BlockCountTest, ComputeBc7RowPitch_ReturnsCorrectPitch)
{
  // Arrange & Act & Assert
  EXPECT_EQ(bc7::ComputeBc7RowPitch(4), 16u); // 1 block
  EXPECT_EQ(bc7::ComputeBc7RowPitch(8), 32u); // 2 blocks
  EXPECT_EQ(bc7::ComputeBc7RowPitch(16), 64u); // 4 blocks
  EXPECT_EQ(bc7::ComputeBc7RowPitch(5), 32u); // 2 blocks (rounded up)
}

//! Test: ComputeBc7SurfaceSize returns correct size.
/*!\
 Verifies surface size is blocks_x * blocks_y * 16 bytes.
*/
NOLINT_TEST_F(Bc7BlockCountTest, ComputeBc7SurfaceSize_ReturnsCorrectSize)
{
  // Arrange & Act & Assert
  EXPECT_EQ(bc7::ComputeBc7SurfaceSize(4, 4), 16u); // 1x1 blocks
  EXPECT_EQ(bc7::ComputeBc7SurfaceSize(8, 8), 64u); // 2x2 blocks
  EXPECT_EQ(bc7::ComputeBc7SurfaceSize(16, 16), 256u); // 4x4 blocks
  EXPECT_EQ(bc7::ComputeBc7SurfaceSize(5, 5), 64u); // 2x2 blocks (rounded)
}

//===----------------------------------------------------------------------===//
// BC7 Single Block Encoding Tests (4.1)
//===----------------------------------------------------------------------===//

class Bc7EncodeBlockTest : public ::testing::Test {
protected:
  void SetUp() override { bc7::InitializeEncoder(); }
};

//! Test: EncodeBlock produces valid BC7 output.
/*!\
 Verifies encoding a solid color block produces non-zero output.
*/
NOLINT_TEST_F(Bc7EncodeBlockTest, EncodeBlock_ProducesOutput)
{
  // Arrange - solid red 4x4 block
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

  // Act
  const bool has_alpha = bc7::EncodeBlock(pixels, output, params);

  // Assert
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

//! Test: EncodeBlock detects alpha.
/*!\
 Verifies encoding a block with alpha returns true.
*/
NOLINT_TEST_F(Bc7EncodeBlockTest, EncodeBlock_DetectsAlpha)
{
  // Arrange - block with partial transparency
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

  // Act
  const bool has_alpha = bc7::EncodeBlock(pixels, output, params);

  // Assert
  EXPECT_TRUE(has_alpha);
}

//===----------------------------------------------------------------------===//
// BC7 Surface Encoding Tests (4.2)
//===----------------------------------------------------------------------===//

class Bc7EncodeSurfaceTest : public ::testing::Test {
protected:
  void SetUp() override { bc7::InitializeEncoder(); }
};

//! Test: EncodeSurface produces valid BC7 image.
/*!\
 Verifies encoding a 4x4 surface produces correctly sized output.
*/
NOLINT_TEST_F(Bc7EncodeSurfaceTest, EncodeSurface_4x4_ProducesValidOutput)
{
  // Arrange - create a 4x4 RGBA8 image
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

  // Act
  auto result = bc7::EncodeSurface(source_view, params);

  // Assert
  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().width, 4u);
  EXPECT_EQ(result.Meta().height, 4u);
  EXPECT_EQ(result.Meta().format, Format::kBC7UNorm);
  EXPECT_EQ(result.GetTotalSizeBytes(), bc7::kBc7BlockSizeBytes);
}

//! Test: EncodeSurface handles non-multiple-of-4 dimensions.
/*!\
 Verifies edge handling with border replication.
*/
NOLINT_TEST_F(Bc7EncodeSurfaceTest, EncodeSurface_NonMultiple4_HandlesEdges)
{
  // Arrange - create a 5x5 RGBA8 image
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

  // Act
  auto result = bc7::EncodeSurface(source_view, params);

  // Assert
  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().width, 5u);
  EXPECT_EQ(result.Meta().height, 5u);
  EXPECT_EQ(result.Meta().format, Format::kBC7UNorm);

  // 5x5 requires 2x2 blocks = 4 blocks * 16 bytes = 64 bytes
  EXPECT_EQ(result.GetTotalSizeBytes(), 64u);
}

//! Test: EncodeSurface fails on invalid format.
/*!\
 Verifies non-RGBA8 input returns invalid result.
*/
NOLINT_TEST_F(Bc7EncodeSurfaceTest, EncodeSurface_InvalidFormat_ReturnsEmpty)
{
  // Arrange - create a float image (wrong format)
  std::vector<std::byte> pixels(4 * 4 * 16); // RGBA32Float
  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA32Float, 64, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  const auto source_view = source.GetImage(0, 0);
  const auto params = bc7::Bc7EncoderParams::Fast();

  // Act
  auto result = bc7::EncodeSurface(source_view, params);

  // Assert
  EXPECT_FALSE(result.IsValid());
}

//===----------------------------------------------------------------------===//
// BC7 Full Texture Encoding Tests (4.2)
//===----------------------------------------------------------------------===//

class Bc7EncodeTextureTest : public ::testing::Test {
protected:
  void SetUp() override { bc7::InitializeEncoder(); }
};

//! Test: EncodeTexture encodes single mip texture.
/*!\
 Verifies full texture encoding with one mip level.
*/
NOLINT_TEST_F(Bc7EncodeTextureTest, EncodeTexture_SingleMip_Succeeds)
{
  // Arrange
  std::vector<std::byte> pixels(8 * 8 * 4);
  for (auto& byte : pixels) {
    byte = std::byte { 128 };
  }

  auto source = ScratchImage::CreateFromData(
    8, 8, Format::kRGBA8UNorm, 32, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  // Act
  auto result = bc7::EncodeTexture(source, bc7::Bc7EncoderParams::Fast());

  // Assert
  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().width, 8u);
  EXPECT_EQ(result.Meta().height, 8u);
  EXPECT_EQ(result.Meta().format, Format::kBC7UNorm);
  EXPECT_EQ(result.Meta().mip_levels, 1u);
}

//! Test: EncodeTexture with quality preset.
/*!\
 Verifies convenience overload with Bc7Quality enum.
*/
NOLINT_TEST_F(Bc7EncodeTextureTest, EncodeTexture_QualityPreset_Works)
{
  // Arrange
  std::vector<std::byte> pixels(4 * 4 * 4);
  for (auto& byte : pixels) {
    byte = std::byte { 200 };
  }

  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA8UNorm, 16, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  // Act
  auto result = bc7::EncodeTexture(source, Bc7Quality::kDefault);

  // Assert
  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().format, Format::kBC7UNorm);
}

//! Test: EncodeTexture with kNone quality returns empty.
/*!\
 Verifies no encoding when quality is kNone.
*/
NOLINT_TEST_F(Bc7EncodeTextureTest, EncodeTexture_QualityNone_ReturnsEmpty)
{
  // Arrange
  std::vector<std::byte> pixels(4 * 4 * 4, std::byte { 128 });
  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA8UNorm, 16, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  // Act
  auto result = bc7::EncodeTexture(source, Bc7Quality::kNone);

  // Assert
  EXPECT_FALSE(result.IsValid());
}

} // namespace
