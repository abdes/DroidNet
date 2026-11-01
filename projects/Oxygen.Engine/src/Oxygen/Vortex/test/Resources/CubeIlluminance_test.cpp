//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Resources/CubeIlluminance.h>
#include <Oxygen/Vortex/test/Fixtures/TextureBinderPayloads.h>

namespace {

using oxygen::Format;
using oxygen::vortex::resources::CalibrationScale;
using oxygen::vortex::resources::MeasureCubeIlluminance;

using Rgb = std::array<float, 3>;

//! A single-mip cube whose texels come from `radiance(face, x, y)`.
/*!
 RGBA32Float texels hold the radiance; any other format repeats the 32-bit
 `packed` texel.
*/
auto MakeCube(const std::uint32_t size,
  const std::function<Rgb(std::uint32_t, std::uint32_t, std::uint32_t)>&
    radiance,
  const Format format = Format::kRGBA32Float, const std::uint32_t packed = 0U)
  -> std::shared_ptr<oxygen::data::TextureResource>
{
  const auto shape = oxygen::vortex::testing::CubeShape(size, format);
  if (format == Format::kRGBA32Float) {
    return oxygen::vortex::testing::MakeSyntheticTexture(shape,
      [&radiance](const std::uint32_t face, const std::uint32_t x,
        const std::uint32_t y) -> std::array<float, 4> {
        const auto rgb = radiance(face, x, y);
        return { rgb[0], rgb[1], rgb[2], 1.0F };
      });
  }
  const auto texel = std::bit_cast<std::array<std::uint8_t, 4>>(packed);
  const auto count = std::size_t { 6U } * size * size;
  auto texels = std::vector<std::uint8_t> {};
  texels.reserve(count * texel.size());
  for (std::size_t i = 0; i < count; ++i) {
    texels.insert(texels.end(), texel.begin(), texel.end());
  }
  return oxygen::vortex::testing::MakeSyntheticTexture(shape, texels);
}

NOLINT_TEST(CubeIlluminanceTest, UniformSkyDeliversPiTimesItsLuminance)
{
  const auto cube = MakeCube(
    32U, [](auto, auto, auto) -> Rgb { return Rgb { 2.0F, 2.0F, 2.0F }; });

  const auto measured = MeasureCubeIlluminance(*cube);

  ASSERT_HAS_VALUE(measured);
  // Per-texel midpoint solid angles at 32 texels per face stay within 0.2%.
  constexpr auto kExpected = 2.0F * std::numbers::pi_v<float>;
  EXPECT_NEAR(*measured, kExpected, kExpected * 2.0e-3F);
}

NOLINT_TEST(CubeIlluminanceTest, OnlyTheUpperHemisphereCounts)
{
  // Cube face 3 (-Y) is straight down in Oxygen world space.
  const auto cube = MakeCube(32U, [](const auto face, auto, auto) -> Rgb {
    return face == 3U ? Rgb { 1000.0F, 1000.0F, 1000.0F } : Rgb {};
  });

  const auto measured = MeasureCubeIlluminance(*cube);

  ASSERT_HAS_VALUE(measured);
  EXPECT_FLOAT_EQ(*measured, 0.0F);
}

NOLINT_TEST(CubeIlluminanceTest, PackedFloatCubesAreMeasured)
{
  constexpr auto kExpected = 2.0F * std::numbers::pi_v<float>;
  const auto none = [](auto, auto, auto) -> Rgb { return Rgb {}; };
  // 2.0 as unsigned small floats: exponent 16 (bias 15), zero mantissa.
  constexpr auto kR11G11B10Two
    = (16U << 6U) | ((16U << 6U) << 11U) | ((16U << 5U) << 22U);
  // 2.0 as a shared exponent: mantissa 256 scaled by 2^(17 - 15 - 9).
  constexpr auto kR9G9B9E5Two
    = 256U | (256U << 9U) | (256U << 18U) | (17U << 27U);

  const auto r11 = MeasureCubeIlluminance(
    *MakeCube(32U, none, Format::kR11G11B10Float, kR11G11B10Two));
  const auto e5 = MeasureCubeIlluminance(
    *MakeCube(32U, none, Format::kR9G9B9E5Float, kR9G9B9E5Two));

  ASSERT_HAS_VALUE(r11);
  EXPECT_NEAR(*r11, kExpected, kExpected * 2.0e-3F);
  ASSERT_HAS_VALUE(e5);
  EXPECT_NEAR(*e5, kExpected, kExpected * 2.0e-3F);
}

NOLINT_TEST(CubeIlluminanceTest, LdrCubesCannotBeMeasured)
{
  const auto cube = MakeCube(
    32U, [](auto, auto, auto) -> Rgb { return Rgb {}; }, Format::kRGBA8UNorm,
    0xFFFFFFFFU);

  EXPECT_FALSE(MeasureCubeIlluminance(*cube).has_value());
}

NOLINT_TEST(CubeIlluminanceTest, CalibrationScaleKeepsUncalibratedSourcesAsIs)
{
  EXPECT_FLOAT_EQ(CalibrationScale(10000.0F, 2.5F), 4000.0F);
  EXPECT_FLOAT_EQ(CalibrationScale(0.0F, 2.5F), 1.0F);
  EXPECT_FLOAT_EQ(CalibrationScale(10000.0F, 0.0F), 1.0F);
}

} // namespace
