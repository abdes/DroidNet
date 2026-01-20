//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/Internal/ImageProcessing.h>
#include <Oxygen/Content/Import/ScratchImage.h>

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
// Color Space Conversion Tests (3.1)
//===----------------------------------------------------------------------===//

class ColorSpaceConversionTest : public ::testing::Test { };

//! Test: SrgbToLinear converts known sRGB values correctly.
/*!\
 Verifies the sRGB to linear conversion at key points.
*/
NOLINT_TEST_F(ColorSpaceConversionTest, SrgbToLinear_ConvertsKnownValues)
{
  // Arrange & Act & Assert
  // Black stays black
  EXPECT_NEAR(color::SrgbToLinear(0.0F), 0.0F, 1e-6F);

  // White stays white
  EXPECT_NEAR(color::SrgbToLinear(1.0F), 1.0F, 1e-6F);

  // Mid-gray (sRGB 0.5 -> linear ~0.214)
  EXPECT_NEAR(color::SrgbToLinear(0.5F), 0.214F, 0.01F);

  // Low values use linear portion
  EXPECT_NEAR(color::SrgbToLinear(0.04045F), 0.04045F / 12.92F, 1e-6F);
}

//! Test: LinearToSrgb converts known linear values correctly.
/*!\
 Verifies the linear to sRGB conversion at key points.
*/
NOLINT_TEST_F(ColorSpaceConversionTest, LinearToSrgb_ConvertsKnownValues)
{
  // Arrange & Act & Assert
  // Black stays black
  EXPECT_NEAR(color::LinearToSrgb(0.0F), 0.0F, 1e-6F);

  // White stays white
  EXPECT_NEAR(color::LinearToSrgb(1.0F), 1.0F, 1e-6F);

  // Linear 0.214 -> sRGB ~0.5
  EXPECT_NEAR(color::LinearToSrgb(0.214F), 0.5F, 0.02F);

  // Low values use linear portion
  EXPECT_NEAR(color::LinearToSrgb(0.001F), 0.001F * 12.92F, 1e-6F);
}

//! Test: Round-trip conversion preserves values.
/*!\
 Verifies that sRGB->linear->sRGB returns original value.
*/
NOLINT_TEST_F(ColorSpaceConversionTest, RoundTrip_PreservesValues)
{
  // Arrange
  constexpr float kTestValues[] = { 0.0F, 0.1F, 0.25F, 0.5F, 0.75F, 1.0F };

  // Act & Assert
  for (const float value : kTestValues) {
    const float linear = color::SrgbToLinear(value);
    const float round_trip = color::LinearToSrgb(linear);
    EXPECT_NEAR(round_trip, value, 1e-5F) << "Failed for value: " << value;
  }
}

//! Test: RGBA conversion preserves alpha.
/*!\
 Verifies that alpha channel is unchanged during conversion.
*/
NOLINT_TEST_F(ColorSpaceConversionTest, RgbaConversion_PreservesAlpha)
{
  // Arrange
  const std::array<float, 4> srgb_rgba = { 0.5F, 0.5F, 0.5F, 0.75F };

  // Act
  const auto linear_rgba = color::SrgbToLinear(srgb_rgba);
  const auto back_to_srgb = color::LinearToSrgb(linear_rgba);

  // Assert
  EXPECT_EQ(linear_rgba[3], 0.75F); // Alpha unchanged
  EXPECT_NEAR(back_to_srgb[3], 0.75F, 1e-6F);
}

//===----------------------------------------------------------------------===//
// HDR Processing Tests (3.2)
//===----------------------------------------------------------------------===//

class HdrProcessingTest : public ::testing::Test { };

//! Test: ApplyExposure scales RGB correctly.
/*!\
 Verifies exposure adjustment using 2^exposure multiplier.
*/
NOLINT_TEST_F(HdrProcessingTest, ApplyExposure_ScalesRgbCorrectly)
{
  // Arrange
  const std::array<float, 4> pixel = { 1.0F, 0.5F, 0.25F, 0.8F };

  // Act - exposure of 1.0 doubles the values
  const auto result = hdr::ApplyExposure(pixel, 1.0F);

  // Assert
  EXPECT_NEAR(result[0], 2.0F, 1e-6F);
  EXPECT_NEAR(result[1], 1.0F, 1e-6F);
  EXPECT_NEAR(result[2], 0.5F, 1e-6F);
  EXPECT_EQ(result[3], 0.8F); // Alpha unchanged
}

//! Test: ApplyExposure with zero exposure returns original.
/*!\
 Verifies that exposure=0 means no change (2^0 = 1).
*/
NOLINT_TEST_F(HdrProcessingTest, ApplyExposure_ZeroExposure_NoChange)
{
  // Arrange
  const std::array<float, 4> pixel = { 0.5F, 0.5F, 0.5F, 1.0F };

  // Act
  const auto result = hdr::ApplyExposure(pixel, 0.0F);

  // Assert
  EXPECT_NEAR(result[0], 0.5F, 1e-6F);
  EXPECT_NEAR(result[1], 0.5F, 1e-6F);
  EXPECT_NEAR(result[2], 0.5F, 1e-6F);
}

//! Test: AcesTonemap maps HDR to [0,1] range.
/*!\
 Verifies that high values are compressed into LDR range.
*/
NOLINT_TEST_F(HdrProcessingTest, AcesTonemap_CompressesHdrToLdr)
{
  // Arrange
  const std::array<float, 4> hdr_pixel = { 10.0F, 5.0F, 1.0F, 1.0F };

  // Act
  const auto result = hdr::AcesTonemap(hdr_pixel);

  // Assert - all values should be in [0,1]
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

//! Test: AcesTonemap preserves black.
/*!\
 Verifies that zero input produces zero output.
*/
NOLINT_TEST_F(HdrProcessingTest, AcesTonemap_PreservesBlack)
{
  // Arrange
  const std::array<float, 4> black = { 0.0F, 0.0F, 0.0F, 1.0F };

  // Act
  const auto result = hdr::AcesTonemap(black);

  // Assert
  EXPECT_NEAR(result[0], 0.0F, 1e-6F);
  EXPECT_NEAR(result[1], 0.0F, 1e-6F);
  EXPECT_NEAR(result[2], 0.0F, 1e-6F);
}

//===----------------------------------------------------------------------===//
// Mip Filter Kernel Tests (3.3)
//===----------------------------------------------------------------------===//

class MipFilterKernelTest : public ::testing::Test { };

//! Test: BesselI0 returns correct values.
/*!\
 Verifies the modified Bessel function at known points.
*/
NOLINT_TEST_F(MipFilterKernelTest, BesselI0_ReturnsCorrectValues)
{
  // Arrange & Act & Assert
  // I0(0) = 1
  EXPECT_NEAR(mip::BesselI0(0.0F), 1.0F, 1e-5F);

  // I0 is even function
  EXPECT_NEAR(mip::BesselI0(1.0F), mip::BesselI0(-1.0F), 1e-5F);

  // I0 is monotonically increasing for positive x
  EXPECT_LT(mip::BesselI0(0.0F), mip::BesselI0(1.0F));
  EXPECT_LT(mip::BesselI0(1.0F), mip::BesselI0(2.0F));
}

//! Test: KaiserWindow returns 1 at center.
/*!\
 Verifies Kaiser window is 1 at x=0.
*/
NOLINT_TEST_F(MipFilterKernelTest, KaiserWindow_ReturnsOneAtCenter)
{
  // Arrange & Act & Assert
  EXPECT_NEAR(mip::KaiserWindow(0.0F, 4.0F), 1.0F, 1e-5F);
}

//! Test: KaiserWindow returns 0 outside range.
/*!\
 Verifies Kaiser window is 0 for |x| > 1.
*/
NOLINT_TEST_F(MipFilterKernelTest, KaiserWindow_ReturnsZeroOutsideRange)
{
  // Arrange & Act & Assert
  EXPECT_EQ(mip::KaiserWindow(1.5F, 4.0F), 0.0F);
  EXPECT_EQ(mip::KaiserWindow(-1.5F, 4.0F), 0.0F);
}

//! Test: LanczosKernel returns 1 at center.
/*!\
 Verifies Lanczos kernel is 1 at x=0.
*/
NOLINT_TEST_F(MipFilterKernelTest, LanczosKernel_ReturnsOneAtCenter)
{
  // Arrange & Act & Assert
  EXPECT_NEAR(mip::LanczosKernel(0.0F, 3), 1.0F, 1e-5F);
}

//! Test: LanczosKernel returns 0 at integer points.
/*!\
 Verifies Lanczos kernel zeros at non-zero integers.
*/
NOLINT_TEST_F(MipFilterKernelTest, LanczosKernel_ReturnsZeroAtIntegers)
{
  // Arrange & Act & Assert
  EXPECT_NEAR(mip::LanczosKernel(1.0F, 3), 0.0F, 1e-5F);
  EXPECT_NEAR(mip::LanczosKernel(2.0F, 3), 0.0F, 1e-5F);
  EXPECT_NEAR(mip::LanczosKernel(-1.0F, 3), 0.0F, 1e-5F);
}

//! Test: LanczosKernel returns 0 outside support.
/*!\
 Verifies Lanczos kernel is 0 for |x| >= a.
*/
NOLINT_TEST_F(MipFilterKernelTest, LanczosKernel_ReturnsZeroOutsideSupport)
{
  // Arrange & Act & Assert
  EXPECT_EQ(mip::LanczosKernel(3.0F, 3), 0.0F);
  EXPECT_EQ(mip::LanczosKernel(-3.0F, 3), 0.0F);
  EXPECT_EQ(mip::LanczosKernel(4.0F, 3), 0.0F);
}

//===----------------------------------------------------------------------===//
// Mip Generation Tests (3.4)
//===----------------------------------------------------------------------===//

class MipGenerationTest : public ::testing::Test { };

//! Test: ComputeMipCount returns correct values.
/*!\
 Verifies mip count calculation for power-of-two dimensions.
*/
NOLINT_TEST_F(MipGenerationTest, ComputeMipCount_ReturnsCorrectValues)
{
  // Arrange & Act & Assert
  EXPECT_EQ(mip::ComputeMipCount(1, 1), 1u);
  EXPECT_EQ(mip::ComputeMipCount(2, 2), 2u);
  EXPECT_EQ(mip::ComputeMipCount(4, 4), 3u);
  EXPECT_EQ(mip::ComputeMipCount(256, 256), 9u);
  EXPECT_EQ(mip::ComputeMipCount(1024, 512), 11u); // max(1024,512) = 1024
}

//! Test: ComputeMipCount handles non-power-of-two dimensions.
/*!\
 Verifies mip count for NPOT textures.
*/
NOLINT_TEST_F(MipGenerationTest, ComputeMipCount_HandlesNpot)
{
  // Arrange & Act & Assert
  EXPECT_EQ(mip::ComputeMipCount(100, 100), 7u); // floor(log2(100))+1 = 7
  EXPECT_EQ(mip::ComputeMipCount(127, 127), 7u);
  EXPECT_EQ(mip::ComputeMipCount(128, 128), 8u);
}

//! Test: GenerateChain2D creates full mip chain.
/*!\
 Verifies mip chain generation with box filter.
*/
NOLINT_TEST_F(MipGenerationTest, GenerateChain2D_CreatesFullChain)
{
  // Arrange - create a 4x4 RGBA8 image
  std::vector<std::byte> pixels(4 * 4 * 4);
  for (size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = std::byte { 128 }; // Mid-gray
  }

  auto source = ScratchImage::CreateFromData(
    4, 4, Format::kRGBA8UNorm, 16, std::move(pixels));
  ASSERT_TRUE(source.IsValid());

  // Act
  auto result
    = mip::GenerateChain2D(source, MipFilter::kBox, ColorSpace::kLinear);

  // Assert
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
// Content-Specific Processing Tests (3.5)
//===----------------------------------------------------------------------===//

class ContentProcessingTest : public ::testing::Test { };

//! Test: RenormalizeNormal preserves unit normals.
/*!\
 Verifies that already-normalized normals are unchanged.
*/
NOLINT_TEST_F(ContentProcessingTest, RenormalizeNormal_PreservesUnitNormals)
{
  // Arrange - up-facing normal (0,0,1) encoded as (0.5, 0.5, 1.0)
  const std::array<float, 4> up_normal = { 0.5F, 0.5F, 1.0F, 1.0F };

  // Act
  const auto result = content::RenormalizeNormal(up_normal);

  // Assert
  EXPECT_NEAR(result[0], 0.5F, 0.01F);
  EXPECT_NEAR(result[1], 0.5F, 0.01F);
  EXPECT_NEAR(result[2], 1.0F, 0.01F);
}

//! Test: RenormalizeNormal normalizes non-unit normals.
/*!\
 Verifies that non-unit normals are normalized.
*/
NOLINT_TEST_F(ContentProcessingTest, RenormalizeNormal_NormalizesNonUnit)
{
  // Arrange - scaled normal that needs renormalization
  // Encoded value (0.75, 0.5, 0.5) -> unpacked (0.5, 0, 0) -> should become (1,
  // 0, 0)
  const std::array<float, 4> scaled_normal = { 0.75F, 0.5F, 0.5F, 1.0F };

  // Act
  const auto result = content::RenormalizeNormal(scaled_normal);

  // Assert - should be normalized +X direction
  // Unpacked: (0.5, 0, 0), normalized: (1, 0, 0), repacked: (1, 0.5, 0.5)
  EXPECT_NEAR(result[0], 1.0F, 0.01F);
  EXPECT_NEAR(result[1], 0.5F, 0.01F);
  EXPECT_NEAR(result[2], 0.5F, 0.01F);
}

//! Test: FlipNormalGreen inverts green channel.
/*!\
 Verifies that green channel is flipped (1 - g).
*/
NOLINT_TEST_F(ContentProcessingTest, FlipNormalGreen_InvertsGreenChannel)
{
  // Arrange - create a 2x2 RGBA8 image
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

  // Act
  content::FlipNormalGreen(image);

  // Assert
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
