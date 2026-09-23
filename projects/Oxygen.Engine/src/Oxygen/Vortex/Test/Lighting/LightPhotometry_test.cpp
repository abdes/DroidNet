//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Internal/LightPhotometry.h>

namespace oxygen::vortex::lighting::internal {
namespace {

  NOLINT_TEST(LightPhotometryTest, DirectionalTintAndCompensationResolveOnce)
  {
    const auto value = ResolveDirectionalIlluminanceRgb(32.0F,
      { .color_rgb = { 0.25F, 0.5F, 1.0F }, .exposure_compensation_ev = 1.0F });
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, (glm::vec3 { 16.0F, 32.0F, 64.0F }));
  }

  NOLINT_TEST(LightPhotometryTest, PointFluxUsesFullSphereSolidAngle)
  {
    const auto value = ResolvePointIntensityRgb(1000.0F, {});
    ASSERT_TRUE(value.has_value());
    const auto expected = 1000.0 / (4.0 * std::numbers::pi);
    EXPECT_NEAR(value->r, expected, expected * 1.0e-7);
    EXPECT_EQ(value->g, value->r);
    EXPECT_EQ(value->b, value->r);
  }

  NOLINT_TEST(LightPhotometryTest, ZeroChannelsAvoidExtremeExponentEvaluation)
  {
    for (const auto ev : {
           std::numeric_limits<float>::max(),
           std::numeric_limits<float>::lowest(),
         }) {
      const auto zero_flux = ResolvePointIntensityRgb(0.0F,
        { .color_rgb = glm::vec3 { 1.0F }, .exposure_compensation_ev = ev });
      const auto zero_tint = ResolveDirectionalIlluminanceRgb(1.0F,
        { .color_rgb = glm::vec3 { 0.0F }, .exposure_compensation_ev = ev });
      ASSERT_TRUE(zero_flux.has_value());
      ASSERT_TRUE(zero_tint.has_value());
      EXPECT_EQ(*zero_flux, glm::vec3 { 0.0F });
      EXPECT_EQ(*zero_tint, glm::vec3 { 0.0F });
    }
  }

  NOLINT_TEST(LightPhotometryTest, TintRescuesUnrepresentableUntintedIntensity)
  {
    const auto value = ResolveDirectionalIlluminanceRgb(std::ldexp(1.0F, 120),
      {
        .color_rgb = { 0.0F, std::ldexp(1.0F, -100), std::ldexp(1.0F, -110) },
        .exposure_compensation_ev = 100.0F,
      });
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value,
      (glm::vec3 { 0.0F, std::ldexp(1.0F, 120), std::ldexp(1.0F, 110) }));
  }

  NOLINT_TEST(LightPhotometryTest, CompensationRescuesUnscaledUnderflow)
  {
    const auto value = ResolveDirectionalIlluminanceRgb(std::ldexp(1.0F, -100),
      {
        .color_rgb = glm::vec3 { std::ldexp(1.0F, -100) },
        .exposure_compensation_ev = 100.0F,
      });
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, glm::vec3 { std::ldexp(1.0F, -100) });
  }

  NOLINT_TEST(LightPhotometryTest, NormalFloatEndpointsAreAccepted)
  {
    for (const auto source : {
           std::numeric_limits<float>::min(),
           std::numeric_limits<float>::max(),
         }) {
      const auto value = ResolveDirectionalIlluminanceRgb(source, {});
      ASSERT_TRUE(value.has_value());
      EXPECT_EQ(*value, glm::vec3 { source });
    }
  }

  NOLINT_TEST(LightPhotometryTest, OverflowAndPositiveUnderflowRejectWholeRgb)
  {
    for (const auto ev : {
           128.0F,
           -127.0F,
           std::numeric_limits<float>::max(),
           std::numeric_limits<float>::lowest(),
         }) {
      const auto value = ResolveDirectionalIlluminanceRgb(1.0F,
        { .color_rgb = { 0.0F, 0.0F, 1.0F }, .exposure_compensation_ev = ev });
      ASSERT_FALSE(value.has_value());
      EXPECT_EQ(value.error(), LightPhotometryError::kUnrepresentable);
    }
  }

  NOLINT_TEST(LightPhotometryTest, InvalidInputsAreRejectedEvenForZeroFlux)
  {
    constexpr auto infinity = std::numeric_limits<float>::infinity();
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    for (const auto source : { -1.0F, infinity, nan }) {
      EXPECT_FALSE(ResolvePointIntensityRgb(source, {}).has_value());
    }
    for (const auto tint : { -1.0F, infinity, nan }) {
      EXPECT_FALSE(
        ResolvePointIntensityRgb(0.0F, { .color_rgb = { 1.0F, tint, 1.0F } })
          .has_value());
    }
    for (const auto ev : { infinity, nan }) {
      EXPECT_FALSE(
        ResolvePointIntensityRgb(0.0F, { .exposure_compensation_ev = ev })
          .has_value());
    }
  }

  NOLINT_TEST(
    LightPhotometryTest, SoftConeFluxMatchesIndependentAngularIntegral)
  {
    constexpr float inner = 0.25F;
    constexpr float outer = 0.75F;
    const auto cone = ResolveSpotConeProfile(inner, outer);
    ASSERT_TRUE(cone.has_value());
    const auto intensity = ResolveSpotIntensityRgb(1000.0F, *cone, {});
    ASSERT_TRUE(intensity.has_value());

    // Integrate the cosine-domain angular profile independently of the
    // production squared-half-angle normalization formula.
    constexpr auto samples = 100000;
    const auto step = static_cast<double>(outer) / samples;
    const auto inner_cos = std::cos(static_cast<double>(inner));
    const auto outer_cos = std::cos(static_cast<double>(outer));
    auto integral = 0.0;
    for (auto sample = 0; sample < samples; ++sample) {
      const auto theta = (sample + 0.5) * step;
      const auto ramp = theta <= inner
        ? 1.0
        : (std::cos(theta) - outer_cos) / (inner_cos - outer_cos);
      integral += ramp * ramp * std::sin(theta) * step;
    }
    integral *= 2.0 * std::numbers::pi;
    EXPECT_NEAR(cone->solid_angle_sr, integral, integral * 1.0e-9);
    EXPECT_NEAR(intensity->r * integral, 1000.0, 1.0e-4);
  }

  NOLINT_TEST(LightPhotometryTest, ConesWithoutFp32AngularSupportAreRejected)
  {
    constexpr float outer = 1.0e-5F;
    EXPECT_EQ(std::cos(outer), 1.0F);
    const auto cone = ResolveSpotConeProfile(0.0F, outer);
    ASSERT_FALSE(cone.has_value());
    EXPECT_EQ(cone.error(), LightPhotometryError::kUnrepresentable);
  }

  NOLINT_TEST(LightPhotometryTest, HemisphereSoftConeAndHardConeHaveFiniteFlux)
  {
    const auto soft
      = ResolveSpotConeProfile(0.0F, std::numbers::pi_v<float> / 2.0F);
    ASSERT_TRUE(soft.has_value());
    EXPECT_EQ(soft->inverse_cosine_width, 1.0F);
    EXPECT_DOUBLE_EQ(soft->solid_angle_sr, 2.0 * std::numbers::pi / 3.0);
    const auto hard = ResolveSpotConeProfile(0.5F, 0.5F);
    ASSERT_TRUE(hard.has_value());
    EXPECT_EQ(hard->inverse_cosine_width, 0.0F);
    EXPECT_FLOAT_EQ(hard->outer_cosine, std::cos(0.5F));
    EXPECT_NEAR(hard->solid_angle_sr,
      2.0 * std::numbers::pi * (1.0 - std::cos(0.5)), 1.0e-15);
  }

  NOLINT_TEST(LightPhotometryTest, InvalidAndUnrepresentableConesAreRejected)
  {
    constexpr auto half_pi = std::numbers::pi_v<float> / 2.0F;
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto invalid = std::array {
      std::array { 0.0F, 0.0F },
      std::array { -0.1F, 0.5F },
      std::array { 0.6F, 0.5F },
      std::array { half_pi, half_pi },
      std::array { 0.0F, std::nextafter(half_pi, 2.0F) },
      std::array { nan, 0.5F },
      std::array { 0.0F, nan },
    };
    for (const auto& angles : invalid) {
      const auto value = ResolveSpotConeProfile(angles.at(0), angles.at(1));
      ASSERT_FALSE(value.has_value());
      EXPECT_EQ(value.error(), LightPhotometryError::kInvalidInput);
    }
    const auto tiny = ResolveSpotConeProfile(0.0F, 1.0e-30F);
    ASSERT_FALSE(tiny.has_value());
    EXPECT_EQ(tiny.error(), LightPhotometryError::kUnrepresentable);
  }

} // namespace
} // namespace oxygen::vortex::lighting::internal
