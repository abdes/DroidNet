//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImageProcessing.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImageProcessing.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ColorSpace;
using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::CoverageMapping;
using oxygen::content::import::MipFilter;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::ScratchImageMeta;
using oxygen::content::import::TextureChannel;

namespace color = oxygen::content::import::image::color;
namespace hdr = oxygen::content::import::image::hdr;
namespace mip = oxygen::content::import::image::mip;
namespace content = oxygen::content::import::image::content;

//===----------------------------------------------------------------------===//
// Color Space Conversion Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(ColorSpaceConversionTest, SrgbToLinearConvertsKnownValues)
{
  // Black stays black
  EXPECT_NEAR(color::SrgbToLinear(0.0F), 0.0F, 1e-6F);

  // White stays white
  EXPECT_NEAR(color::SrgbToLinear(1.0F), 1.0F, 1e-6F);

  // Mid-gray (sRGB 0.5 -> linear ~0.214)
  EXPECT_NEAR(color::SrgbToLinear(0.5F), 0.214F, 0.01F);

  // Low values use linear portion
  EXPECT_NEAR(color::SrgbToLinear(0.04045F), 0.04045F / 12.92F, 1e-6F);
}

NOLINT_TEST(ColorSpaceConversionTest, LinearToSrgbConvertsKnownValues)
{
  // Black stays black
  EXPECT_NEAR(color::LinearToSrgb(0.0F), 0.0F, 1e-6F);

  // White stays white
  EXPECT_NEAR(color::LinearToSrgb(1.0F), 1.0F, 1e-6F);

  // Linear 0.214 -> sRGB ~0.5
  EXPECT_NEAR(color::LinearToSrgb(0.214F), 0.5F, 0.02F);

  // Low values use linear portion
  EXPECT_NEAR(color::LinearToSrgb(0.001F), 0.001F * 12.92F, 1e-6F);
}

NOLINT_TEST(ColorSpaceConversionTest, RoundTripPreservesValues)
{
  constexpr auto kTestValues
    = std::array { 0.0F, 0.1F, 0.25F, 0.5F, 0.75F, 1.0F };

  for (const float value : kTestValues) {
    const float linear = color::SrgbToLinear(value);
    const float round_trip = color::LinearToSrgb(linear);
    EXPECT_NEAR(round_trip, value, 1e-5F) << "Failed for value: " << value;
  }
}

NOLINT_TEST(ColorSpaceConversionTest, RgbaConversionPreservesAlpha)
{
  const std::array<float, 4> srgb_rgba = { 0.5F, 0.5F, 0.5F, 0.75F };

  const auto linear_rgba = color::SrgbToLinear(srgb_rgba);
  const auto back_to_srgb = color::LinearToSrgb(linear_rgba);

  EXPECT_EQ(linear_rgba.at(3), 0.75F); // Alpha unchanged
  EXPECT_NEAR(back_to_srgb.at(3), 0.75F, 1e-6F);
}

//===----------------------------------------------------------------------===//
// HDR Processing Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(HdrProcessingTest, ApplyExposureScalesRgbCorrectly)
{
  const std::array<float, 4> pixel = { 1.0F, 0.5F, 0.25F, 0.8F };

  // Exposure of 1.0 doubles the values
  const auto result = hdr::ApplyExposure(pixel, 1.0F);

  EXPECT_NEAR(result.at(0), 2.0F, 1e-6F);
  EXPECT_NEAR(result.at(1), 1.0F, 1e-6F);
  EXPECT_NEAR(result.at(2), 0.5F, 1e-6F);
  EXPECT_EQ(result.at(3), 0.8F); // Alpha unchanged
}

NOLINT_TEST(HdrProcessingTest, ApplyExposureZeroExposureNoChange)
{
  const std::array<float, 4> pixel = { 0.5F, 0.5F, 0.5F, 1.0F };

  const auto result = hdr::ApplyExposure(pixel, 0.0F);

  EXPECT_NEAR(result.at(0), 0.5F, 1e-6F);
  EXPECT_NEAR(result.at(1), 0.5F, 1e-6F);
  EXPECT_NEAR(result.at(2), 0.5F, 1e-6F);
}

NOLINT_TEST(HdrProcessingTest, AcesTonemapCompressesHdrToLdr)
{
  const std::array<float, 4> hdr_pixel = { 10.0F, 5.0F, 1.0F, 1.0F };

  const auto result = hdr::AcesTonemap(hdr_pixel);

  // All values should be in [0,1]
  EXPECT_GE(result.at(0), 0.0F);
  EXPECT_LE(result.at(0), 1.0F);
  EXPECT_GE(result.at(1), 0.0F);
  EXPECT_LE(result.at(1), 1.0F);
  EXPECT_GE(result.at(2), 0.0F);
  EXPECT_LE(result.at(2), 1.0F);

  // Higher input should result in higher output
  EXPECT_GT(result.at(0), result.at(1));
  EXPECT_GT(result.at(1), result.at(2));
}

NOLINT_TEST(HdrProcessingTest, AcesTonemapPreservesBlack)
{
  const std::array<float, 4> black = { 0.0F, 0.0F, 0.0F, 1.0F };

  const auto result = hdr::AcesTonemap(black);

  EXPECT_NEAR(result.at(0), 0.0F, 1e-6F);
  EXPECT_NEAR(result.at(1), 0.0F, 1e-6F);
  EXPECT_NEAR(result.at(2), 0.0F, 1e-6F);
}

//===----------------------------------------------------------------------===//
// Mip Filter Kernel Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(MipFilterKernelTest, BesselI0ReturnsCorrectValues)
{
  // I0(0) = 1
  EXPECT_NEAR(mip::BesselI0(0.0F), 1.0F, 1e-5F);

  // I0 is even function
  EXPECT_NEAR(mip::BesselI0(1.0F), mip::BesselI0(-1.0F), 1e-5F);

  // I0 is monotonically increasing for positive x
  EXPECT_LT(mip::BesselI0(0.0F), mip::BesselI0(1.0F));
  EXPECT_LT(mip::BesselI0(1.0F), mip::BesselI0(2.0F));
}

NOLINT_TEST(MipFilterKernelTest, KaiserWindowReturnsOneAtCenter)
{
  EXPECT_NEAR(mip::KaiserWindow(0.0F, 4.0F), 1.0F, 1e-5F);
}

NOLINT_TEST(MipFilterKernelTest, KaiserWindowReturnsZeroOutsideRange)
{
  EXPECT_EQ(mip::KaiserWindow(1.5F, 4.0F), 0.0F);
  EXPECT_EQ(mip::KaiserWindow(-1.5F, 4.0F), 0.0F);
}

NOLINT_TEST(MipFilterKernelTest, LanczosKernelReturnsOneAtCenter)
{
  EXPECT_NEAR(mip::LanczosKernel(0.0F), 1.0F, 1e-5F);
}

NOLINT_TEST(MipFilterKernelTest, LanczosKernelReturnsZeroAtIntegers)
{
  EXPECT_NEAR(mip::LanczosKernel(1.0F), 0.0F, 1e-5F);
  EXPECT_NEAR(mip::LanczosKernel(2.0F), 0.0F, 1e-5F);
  EXPECT_NEAR(mip::LanczosKernel(-1.0F), 0.0F, 1e-5F);
}

NOLINT_TEST(MipFilterKernelTest, LanczosKernelReturnsZeroOutsideSupport)
{
  EXPECT_EQ(mip::LanczosKernel(3.0F), 0.0F);
  EXPECT_EQ(mip::LanczosKernel(-3.0F), 0.0F);
  EXPECT_EQ(mip::LanczosKernel(4.0F), 0.0F);
}

//===----------------------------------------------------------------------===//
// Mip Generation Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(MipGenerationTest, GenerateChain2DCreatesFullChain)
{
  // Create a 4x4 RGBA8 image
  std::vector<std::byte> pixels(std::size_t { 4 } * 4 * 4);
  for (auto& pixel : pixels) {
    pixel = std::byte { 128 }; // Mid-gray
  }

  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA8UNorm, 16, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  auto result
    = mip::GenerateChain2D(source, MipFilter::kBox, ColorSpace::kLinear);

  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().mip_levels, 3U); // 4x4 -> 2x2 -> 1x1
  EXPECT_EQ(result.Meta().width, 4U);
  EXPECT_EQ(result.Meta().height, 4U);

  // Check mip 1 dimensions
  const auto mip1 = result.GetImage(0, 1);
  EXPECT_EQ(mip1.width, 2U);
  EXPECT_EQ(mip1.height, 2U);

  // Check mip 2 dimensions
  const auto mip2 = result.GetImage(0, 2);
  EXPECT_EQ(mip2.width, 1U);
  EXPECT_EQ(mip2.height, 1U);
}

//===----------------------------------------------------------------------===//
// Content-Specific Processing Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(ContentProcessingTest, RenormalizeNormalPreservesUnitNormals)
{
  // Up-facing normal (0,0,1) encoded as (0.5, 0.5, 1.0)
  const std::array<float, 4> up_normal = { 0.5F, 0.5F, 1.0F, 1.0F };

  const auto result = content::RenormalizeNormal(up_normal);

  EXPECT_NEAR(result.at(0), 0.5F, 0.01F);
  EXPECT_NEAR(result.at(1), 0.5F, 0.01F);
  EXPECT_NEAR(result.at(2), 1.0F, 0.01F);
}

NOLINT_TEST(ContentProcessingTest, RenormalizeNormalNormalizesNonUnit)
{
  // Scaled normal that needs renormalization
  // Encoded value (0.75, 0.5, 0.5) -> unpacked (0.5, 0, 0) -> should become (1,
  // 0, 0)
  const std::array<float, 4> scaled_normal = { 0.75F, 0.5F, 0.5F, 1.0F };

  const auto result = content::RenormalizeNormal(scaled_normal);

  // Should be normalized +X direction
  // Unpacked: (0.5, 0, 0), normalized: (1, 0, 0), repacked: (1, 0.5, 0.5)
  EXPECT_NEAR(result.at(0), 1.0F, 0.01F);
  EXPECT_NEAR(result.at(1), 0.5F, 0.01F);
  EXPECT_NEAR(result.at(2), 0.5F, 0.01F);
}

NOLINT_TEST(ContentProcessingTest, FlipNormalGreenInvertsGreenChannel)
{
  // Create a 2x2 RGBA8 image
  std::vector<std::byte> pixels(std::size_t { 2 } * 2 * 4);
  for (size_t i = 0; i < 4; ++i) {
    const size_t offset = i * 4;
    pixels.at(offset + 0) = std::byte { 128 }; // R
    pixels.at(offset + 1) = std::byte { 64 }; // G = 64
    pixels.at(offset + 2) = std::byte { 255 }; // B
    pixels.at(offset + 3) = std::byte { 255 }; // A
  }

  auto image = ScratchImage::CreateFromData(
    2, 2, Format::kRGBA8UNorm, 8, std::move(pixels));
  ASSERT_TRUE(image.IsValid());

  content::FlipNormalGreen(image);

  const auto view = image.GetImage(0, 0);

  for (size_t i = 0; i < 4; ++i) {
    const size_t offset = i * 4;
    const auto r = std::to_integer<unsigned>(view.pixels[offset + 0]);
    const auto g = std::to_integer<unsigned>(view.pixels[offset + 1]);
    const auto b = std::to_integer<unsigned>(view.pixels[offset + 2]);
    const auto a = std::to_integer<unsigned>(view.pixels[offset + 3]);
    EXPECT_EQ(r, 128U); // R unchanged
    EXPECT_EQ(g, 191U); // G flipped: 255 - 64 = 191
    EXPECT_EQ(b, 255U); // B unchanged
    EXPECT_EQ(a, 255U); // A unchanged
  }
}

NOLINT_TEST(
  ContentProcessingTest, MoveValueToRedCopiesTheChannelAndOpaquesAlpha)
{
  std::vector<std::byte> pixels(std::size_t { 2 } * 2 * 4);
  for (size_t i = 0; i < 4; ++i) {
    const size_t offset = i * 4;
    pixels.at(offset + 0) = std::byte { 10 };
    pixels.at(offset + 1) = std::byte { 20 };
    pixels.at(offset + 2) = std::byte { 30 };
    pixels.at(offset + 3) = std::byte { 77 }; // the value, as glTF stores it
  }
  auto image = ScratchImage::CreateFromData(
    2, 2, Format::kRGBA8UNorm, 8, std::move(pixels));
  ASSERT_TRUE(image.IsValid());

  content::MoveValueToRed(image, TextureChannel::kAlpha);

  const auto view = image.GetImage(0, 0);
  for (size_t i = 0; i < 4; ++i) {
    const size_t offset = i * 4;
    EXPECT_EQ(std::to_integer<unsigned>(view.pixels[offset + 0]), 77U);
    EXPECT_EQ(std::to_integer<unsigned>(view.pixels[offset + 1]), 77U);
    EXPECT_EQ(std::to_integer<unsigned>(view.pixels[offset + 2]), 77U);
    EXPECT_EQ(std::to_integer<unsigned>(view.pixels[offset + 3]), 255U);
  }
}

//! A single RGBA8 texel.
auto Texel8(const uint8_t r, const uint8_t g, const uint8_t b, const uint8_t a)
  -> ScratchImage
{
  return ScratchImage::CreateFromData(1, 1, Format::kRGBA8UNorm, 4,
    { std::byte { r }, std::byte { g }, std::byte { b }, std::byte { a } });
}

//! The alpha of the single texel of `image`.
auto AlphaOf(const ScratchImage& image) -> unsigned
{
  return std::to_integer<unsigned>(image.GetImage(0, 0).pixels[3]);
}

//! An opacity map holds its value in its alpha when it has one, else in its
//! red, as a grey mask does.
NOLINT_TEST(ContentProcessingTest, CoverageIsInAlphaWhenTheMapHasOne)
{
  EXPECT_EQ(
    content::CoverageChannel(Texel8(9, 9, 9, 255)), TextureChannel::kRed);
  EXPECT_EQ(
    content::CoverageChannel(Texel8(9, 9, 9, 254)), TextureChannel::kAlpha);
}

//! A transparency map gives one minus its value scaled by its factor; an
//! opacity map its value scaled.
NOLINT_TEST(ContentProcessingTest, ReplaceAlphaAppliesTheMapping)
{
  auto transparent = Texel8(10, 20, 30, 255);
  auto opaque = Texel8(10, 20, 30, 255);
  const auto mask = Texel8(204, 0, 0, 255); // 0.8 in red

  ASSERT_TRUE(content::ReplaceAlpha(
    transparent, mask, CoverageMapping { .factor = 0.5F, .inverted = true }));
  ASSERT_TRUE(content::ReplaceAlpha(
    opaque, mask, CoverageMapping { .factor = 0.5F, .inverted = false }));

  EXPECT_EQ(AlphaOf(transparent), 153U); // (1 - 0.5 x 0.8) x 255
  EXPECT_EQ(AlphaOf(opaque), 102U); // 0.5 x 0.8 x 255
}

//! A volume is no single 2D image: `ReplaceAlpha` refuses it and leaves the
//! colour image as it was.
NOLINT_TEST(ContentProcessingTest, ReplaceAlphaRefusesAVolumeUnchanged)
{
  auto colour = Texel8(10, 20, 30, 77);
  auto volume = ScratchImage::Create(ScratchImageMeta {
    .texture_type = TextureType::kTexture3D,
    .width = 1,
    .height = 1,
    .depth = 2,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA8UNorm,
  });
  ASSERT_TRUE(volume.IsValid());

  EXPECT_FALSE(content::ReplaceAlpha(colour, volume, CoverageMapping {}));
  EXPECT_FALSE(content::ReplaceAlpha(volume, colour, CoverageMapping {}));
  EXPECT_EQ(AlphaOf(colour), 77U);
}

//! `ReplaceAlpha` writes the coverage into alpha, scaled from its format's
//! range, keeps the colour, and refuses a coverage of another size, leaving
//! the image as it was.
NOLINT_TEST(ContentProcessingTest, ReplaceAlphaTakesTheCoverageChannel)
{
  auto colour = ScratchImage::CreateFromData(1, 1, Format::kRGBA8UNorm, 4,
    {
      std::byte { 10 },
      std::byte { 20 },
      std::byte { 30 },
      std::byte { 255 },
    });
  const auto half = std::array { 0.5F, 0.0F, 0.0F, 1.0F };
  auto coverage_bytes = std::vector<std::byte>(sizeof(half));
  std::memcpy(coverage_bytes.data(), half.data(), sizeof(half));
  const auto coverage = ScratchImage::CreateFromData(
    1, 1, Format::kRGBA32Float, sizeof(half), std::move(coverage_bytes));
  const auto larger = ScratchImage::CreateFromData(
    2, 1, Format::kRGBA8UNorm, 8, std::vector<std::byte>(8));
  ASSERT_TRUE(colour.IsValid());
  ASSERT_TRUE(coverage.IsValid());
  ASSERT_TRUE(larger.IsValid());

  const auto refused
    = content::ReplaceAlpha(colour, larger, CoverageMapping {});
  const auto unchanged = AlphaOf(colour);
  const auto applied
    = content::ReplaceAlpha(colour, coverage, CoverageMapping {});

  EXPECT_FALSE(refused);
  EXPECT_EQ(unchanged, 255U);
  EXPECT_TRUE(applied);
  const auto view = colour.GetImage(0, 0);
  EXPECT_EQ(std::to_integer<unsigned>(view.pixels[0]), 10U);
  EXPECT_EQ(std::to_integer<unsigned>(view.pixels[1]), 20U);
  EXPECT_EQ(std::to_integer<unsigned>(view.pixels[2]), 30U);
  EXPECT_EQ(std::to_integer<unsigned>(view.pixels[3]), 128U);
}

} // namespace
