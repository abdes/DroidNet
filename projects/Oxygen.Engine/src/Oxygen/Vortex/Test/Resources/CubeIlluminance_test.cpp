//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Resources/CubeIlluminance.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
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
  const auto texel_bytes = format == Format::kRGBA32Float ? 16U : 4U;
  namespace pak = oxygen::data::pak;
  auto desc = pak::core::TextureResourceDesc {};
  desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
  desc.width = desc.height = size;
  desc.depth = desc.mip_levels = 1U;
  desc.array_layers = 6U;
  desc.format = static_cast<std::uint8_t>(format);
  desc.alignment = 256U;
  const auto face_bytes = size * size * texel_bytes;
  auto bytes = std::vector<std::uint8_t>(6U * face_bytes);
  auto layouts = std::vector<pak::render::SubresourceLayout> {};
  for (auto face = 0U; face < 6U; ++face) {
    for (auto y = 0U; y < size; ++y) {
      for (auto x = 0U; x < size; ++x) {
        auto* dst
          = bytes.data() + (face * face_bytes) + ((y * size + x) * texel_bytes);
        if (format == Format::kRGBA32Float) {
          const auto rgb = radiance(face, x, y);
          const std::array<float, 4> texel { rgb[0], rgb[1], rgb[2], 1.0F };
          std::memcpy(dst, texel.data(), sizeof(texel));
        } else {
          std::memcpy(dst, &packed, sizeof(packed));
        }
      }
    }
    layouts.push_back({ .offset_bytes = face * face_bytes,
      .row_pitch_bytes = size * texel_bytes,
      .size_bytes = face_bytes });
  }
  auto payload = oxygen::vortex::testing::detail::BuildV4TexturePayload(
    desc, layouts, bytes);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());
  return std::make_shared<oxygen::data::TextureResource>(
    desc, std::move(payload));
}

NOLINT_TEST(CubeIlluminanceTest, UniformSkyDeliversPiTimesItsLuminance)
{
  const auto cube
    = MakeCube(32U, [](auto, auto, auto) { return Rgb { 2.0F, 2.0F, 2.0F }; });

  const auto measured = MeasureCubeIlluminance(*cube);

  ASSERT_TRUE(measured.has_value());
  // Per-texel midpoint solid angles at 32 texels per face stay within 0.2%.
  constexpr auto kExpected = 2.0F * std::numbers::pi_v<float>;
  EXPECT_NEAR(*measured, kExpected, kExpected * 2.0e-3F);
}

NOLINT_TEST(CubeIlluminanceTest, OnlyTheUpperHemisphereCounts)
{
  // Cube face 3 (-Y) is straight down in Oxygen world space.
  const auto cube = MakeCube(32U, [](const auto face, auto, auto) {
    return face == 3U ? Rgb { 1000.0F, 1000.0F, 1000.0F } : Rgb {};
  });

  const auto measured = MeasureCubeIlluminance(*cube);

  ASSERT_TRUE(measured.has_value());
  EXPECT_FLOAT_EQ(*measured, 0.0F);
}

NOLINT_TEST(CubeIlluminanceTest, PackedFloatCubesAreMeasured)
{
  constexpr auto kExpected = 2.0F * std::numbers::pi_v<float>;
  const auto none = [](auto, auto, auto) { return Rgb {}; };
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

  ASSERT_TRUE(r11.has_value());
  EXPECT_NEAR(*r11, kExpected, kExpected * 2.0e-3F);
  ASSERT_TRUE(e5.has_value());
  EXPECT_NEAR(*e5, kExpected, kExpected * 2.0e-3F);
}

NOLINT_TEST(CubeIlluminanceTest, LdrCubesCannotBeMeasured)
{
  const auto cube = MakeCube(
    32U, [](auto, auto, auto) { return Rgb {}; }, Format::kRGBA8UNorm,
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
