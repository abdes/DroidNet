//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImageProcessing.cpp

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImageProcessing.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ColorSpace;
using oxygen::Format;
using oxygen::content::import::MipFilter;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::ScratchImageMeta;

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
  constexpr float kTestValues[] = { 0.0F, 0.1F, 0.25F, 0.5F, 0.75F, 1.0F };

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

  EXPECT_EQ(linear_rgba[3], 0.75F); // Alpha unchanged
  EXPECT_NEAR(back_to_srgb[3], 0.75F, 1e-6F);
}

//===----------------------------------------------------------------------===//
// HDR Processing Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(HdrProcessingTest, ApplyExposureScalesRgbCorrectly)
{
  const std::array<float, 4> pixel = { 1.0F, 0.5F, 0.25F, 0.8F };

  // Exposure of 1.0 doubles the values
  const auto result = hdr::ApplyExposure(pixel, 1.0F);

  EXPECT_NEAR(result[0], 2.0F, 1e-6F);
  EXPECT_NEAR(result[1], 1.0F, 1e-6F);
  EXPECT_NEAR(result[2], 0.5F, 1e-6F);
  EXPECT_EQ(result[3], 0.8F); // Alpha unchanged
}

NOLINT_TEST(HdrProcessingTest, ApplyExposureZeroExposureNoChange)
{
  const std::array<float, 4> pixel = { 0.5F, 0.5F, 0.5F, 1.0F };

  const auto result = hdr::ApplyExposure(pixel, 0.0F);

  EXPECT_NEAR(result[0], 0.5F, 1e-6F);
  EXPECT_NEAR(result[1], 0.5F, 1e-6F);
  EXPECT_NEAR(result[2], 0.5F, 1e-6F);
}

NOLINT_TEST(HdrProcessingTest, AcesTonemapCompressesHdrToLdr)
{
  const std::array<float, 4> hdr_pixel = { 10.0F, 5.0F, 1.0F, 1.0F };

  const auto result = hdr::AcesTonemap(hdr_pixel);

  // All values should be in [0,1]
  EXPECT_GE(result[0], 0.0F);
  EXPECT_LE(result[0], 1.0F);
  EXPECT_GE(result[1], 0.0F);
  EXPECT_LE(result[1], 1.0F);
  EXPECT_GE(result[2], 0.0F);
  EXPECT_LE(result[2], 1.0F);

  // Higher input should result in higher output
  EXPECT_GT(result[0], result[1]);
  EXPECT_GT(result[1], result[2]);
}

NOLINT_TEST(HdrProcessingTest, AcesTonemapPreservesBlack)
{
  const std::array<float, 4> black = { 0.0F, 0.0F, 0.0F, 1.0F };

  const auto result = hdr::AcesTonemap(black);

  EXPECT_NEAR(result[0], 0.0F, 1e-6F);
  EXPECT_NEAR(result[1], 0.0F, 1e-6F);
  EXPECT_NEAR(result[2], 0.0F, 1e-6F);
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
  EXPECT_NEAR(mip::LanczosKernel(0.0F, 3), 1.0F, 1e-5F);
}

NOLINT_TEST(MipFilterKernelTest, LanczosKernelReturnsZeroAtIntegers)
{
  EXPECT_NEAR(mip::LanczosKernel(1.0F, 3), 0.0F, 1e-5F);
  EXPECT_NEAR(mip::LanczosKernel(2.0F, 3), 0.0F, 1e-5F);
  EXPECT_NEAR(mip::LanczosKernel(-1.0F, 3), 0.0F, 1e-5F);
}

NOLINT_TEST(MipFilterKernelTest, LanczosKernelReturnsZeroOutsideSupport)
{
  EXPECT_EQ(mip::LanczosKernel(3.0F, 3), 0.0F);
  EXPECT_EQ(mip::LanczosKernel(-3.0F, 3), 0.0F);
  EXPECT_EQ(mip::LanczosKernel(4.0F, 3), 0.0F);
}

//===----------------------------------------------------------------------===//
// Mip Generation Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(MipGenerationTest, ComputeMipCountReturnsCorrectValues)
{
  EXPECT_EQ(mip::ComputeMipCount(1, 1), 1u);
  EXPECT_EQ(mip::ComputeMipCount(2, 2), 2u);
  EXPECT_EQ(mip::ComputeMipCount(4, 4), 3u);
  EXPECT_EQ(mip::ComputeMipCount(256, 256), 9u);
  EXPECT_EQ(mip::ComputeMipCount(1024, 512), 11u); // max(1024,512) = 1024
}

NOLINT_TEST(MipGenerationTest, ComputeMipCountHandlesNpot)
{
  EXPECT_EQ(mip::ComputeMipCount(100, 100), 7u); // floor(log2(100))+1 = 7
  EXPECT_EQ(mip::ComputeMipCount(127, 127), 7u);
  EXPECT_EQ(mip::ComputeMipCount(128, 128), 8u);
}

NOLINT_TEST(MipGenerationTest, GenerateChain2DCreatesFullChain)
{
  // Create a 4x4 RGBA8 image
  std::vector<std::byte> pixels(4 * 4 * 4);
  for (size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = std::byte { 128 }; // Mid-gray
  }

  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA8UNorm, 16, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  auto result
    = mip::GenerateChain2D(source, MipFilter::kBox, ColorSpace::kLinear);

  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.Meta().mip_levels, 3u); // 4x4 -> 2x2 -> 1x1
  EXPECT_EQ(result.Meta().width, 4u);
  EXPECT_EQ(result.Meta().height, 4u);

  // Check mip 1 dimensions
  const auto mip1 = result.GetImage(0, 1);
  EXPECT_EQ(mip1.width, 2u);
  EXPECT_EQ(mip1.height, 2u);

  // Check mip 2 dimensions
  const auto mip2 = result.GetImage(0, 2);
  EXPECT_EQ(mip2.width, 1u);
  EXPECT_EQ(mip2.height, 1u);
}

//===----------------------------------------------------------------------===//
// Content-Specific Processing Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(ContentProcessingTest, RenormalizeNormalPreservesUnitNormals)
{
  // Up-facing normal (0,0,1) encoded as (0.5, 0.5, 1.0)
  const std::array<float, 4> up_normal = { 0.5F, 0.5F, 1.0F, 1.0F };

  const auto result = content::RenormalizeNormal(up_normal);

  EXPECT_NEAR(result[0], 0.5F, 0.01F);
  EXPECT_NEAR(result[1], 0.5F, 0.01F);
  EXPECT_NEAR(result[2], 1.0F, 0.01F);
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
  EXPECT_NEAR(result[0], 1.0F, 0.01F);
  EXPECT_NEAR(result[1], 0.5F, 0.01F);
  EXPECT_NEAR(result[2], 0.5F, 0.01F);
}

NOLINT_TEST(ContentProcessingTest, FlipNormalGreenInvertsGreenChannel)
{
  // Create a 2x2 RGBA8 image
  std::vector<std::byte> pixels(2 * 2 * 4);
  for (size_t i = 0; i < 4; ++i) {
    const size_t offset = i * 4;
    pixels[offset + 0] = std::byte { 128 }; // R
    pixels[offset + 1] = std::byte { 64 }; // G = 64
    pixels[offset + 2] = std::byte { 255 }; // B
    pixels[offset + 3] = std::byte { 255 }; // A
  }

  auto image = ScratchImage::CreateFromData(
    2, 2, Format::kRGBA8UNorm, 8, std::move(pixels));
  ASSERT_TRUE(image.IsValid());

  content::FlipNormalGreen(image);

  const auto view = image.GetImage(0, 0);
  const auto* data = reinterpret_cast<const uint8_t*>(view.pixels.data());

  for (size_t i = 0; i < 4; ++i) {
    const size_t offset = i * 4;
    EXPECT_EQ(data[offset + 0], 128u); // R unchanged
    EXPECT_EQ(data[offset + 1], 191u); // G flipped: 255 - 64 = 191
    EXPECT_EQ(data[offset + 2], 255u); // B unchanged
    EXPECT_EQ(data[offset + 3], 255u); // A unchanged
  }
}

} // namespace
