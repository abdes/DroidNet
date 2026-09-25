//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>

namespace oxygen::vortex::testing {
namespace {

  struct FogInput {
    EnvironmentStaticData environment;
    EnvironmentViewData view;
    std::array<float, 3> origin { 0.0F, 0.0F, 1.0F };
    float distance { 100.0F };
    std::array<float, 3> direction { 1.0F, 0.0F, 0.0F };
    std::uint32_t mode { 0U };
  };
  static_assert(sizeof(FogInput) == 1024U);

  auto BaseFog() -> FogInput
  {
    auto result = FogInput {};
    auto& fog = result.environment.fog;
    fog.flags = kGpuFogFlagEnabled | kGpuFogFlagHeightFogEnabled
      | kGpuFogFlagRenderInMainPass | kGpuFogFlagVisibleInRealTimeSkyCaptures;
    fog.primary_density = 0.0002F;
    fog.primary_height_falloff = 0.001F;
    fog.primary_height_offset_m = 100.0F;
    fog.secondary_density = 0.0004F;
    fog.secondary_height_falloff = 0.0004F;
    fog.secondary_height_offset_m = -50.0F;
    fog.min_transmittance = 0.2F;
    fog.max_opacity = 0.8F;
    fog.fog_inscattering_luminance_rgb = { 0.7F, 0.3F, 0.2F };
    result.view.flags = kEnvironmentViewFlagHeightFog;
    return result;
  }

  auto Distance(const FogInput& input) -> double
  {
    if (input.mode == 2U)
      return input.distance;
    return 0.05
      / (1.0e-10
        * std::max({ std::abs(double(input.direction[0])),
          std::abs(double(input.direction[1])),
          std::abs(double(input.direction[2])) }));
  }

  // Exact double exponential integration, with the established clipping order.
  auto ReferenceOpacity(const FogInput& input) -> double
  {
    const auto& f = input.environment.fog;
    const auto distance = Distance(input);
    auto ray = std::array<double, 3> { input.direction[0] * distance,
      input.direction[1] * distance, input.direction[2] * distance };
    auto observer = double(input.origin[2]);
    if (f.primary_density > 0.0F)
      observer
        = std::min(observer, double(f.primary_height_offset_m) + 65536.0);
    if (f.secondary_density > 0.0F)
      observer
        = std::min(observer, double(f.secondary_height_offset_m) + 65536.0);
    const auto xy_length = std::hypot(ray[0], ray[1]);
    if (f.end_distance_m > 0.0F && xy_length > f.end_distance_m)
      for (auto& value : ray)
        value *= f.end_distance_m / std::max(1.0, xy_length);
    ray[2] += input.origin[2] - observer;
    const auto length
      = std::sqrt(ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
    if (f.cutoff_distance_m > 0.0F && length > f.cutoff_distance_m)
      return 0.0;
    const auto exclude = length > 0.001
      ? std::clamp(double(f.start_distance_m) / length, 0.0, 1.0)
      : 0.0;
    const auto z = observer + exclude * ray[2];
    const auto dz = (1.0 - exclude) * ray[2];
    const auto integrate = [&](double density, double falloff, double height) {
      const auto origin_density = density
        * std::exp2(std::clamp(-falloff * (z - height), -125.0, 126.0));
      const auto q = std::max(-127.0, falloff * dz);
      const auto integral = std::abs(q) < 1.0e-9
        ? std::log(2.0)
        : -std::expm1(-std::log(2.0) * q) / q;
      return origin_density * integral * (1.0 - exclude) * length;
    };
    const auto optical = integrate(f.primary_density, f.primary_height_falloff,
                           f.primary_height_offset_m)
      + integrate(f.secondary_density, f.secondary_height_falloff,
        f.secondary_height_offset_m);
    return std::min(
      -std::expm1(-optical * std::log(2.0)), 1.0 - f.min_transmittance);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, HeightFogDistantRaysMatchIndependentIntegral)
  {
    auto inputs = std::vector<FogInput> {};
    for (auto direction :
      { std::array { 1.0F, 0.0F, 0.0F }, std::array { 1.0F, 1.0F, 0.0F },
        std::array { 1.0F, 1.0F, 1.0F }, std::array { 0.0F, 0.0F, 1.0F },
        std::array { 0.0F, 0.0F, -1.0F }, std::array { 1.0F, 0.0F, -0.4F } }) {
      const auto norm = std::sqrt(direction[0] * direction[0]
        + direction[1] * direction[1] + direction[2] * direction[2]);
      for (auto& value : direction)
        value /= norm;
      for (const auto altitude : { -300.0F, 1.0F, 65590.0F, 100000.0F }) {
        for (auto variant = 0U; variant < 5U; ++variant) {
          auto input = BaseFog();
          input.direction = direction;
          input.origin[2] = altitude;
          if (variant == 1U)
            input.environment.fog.end_distance_m = 1000.0F;
          if (variant == 2U) {
            input.environment.fog.end_distance_m = 10000.0F;
            input.environment.fog.start_distance_m = 4000.0F;
          }
          if (variant == 3U)
            input.environment.fog.cutoff_distance_m = 100000.0F;
          if (variant == 4U) {
            input.mode = 2U;
            input.distance = 200.0F;
            input.environment.fog.start_distance_m = 5.0F;
          }
          inputs.push_back(input);
        }
      }
    }
    const auto result = Decode({ .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(FogInput),
      .record_kind = 34U,
      .decoded_words = 8U,
      .count = static_cast<std::uint32_t>(inputs.size()) });
    for (auto i = 0U; i < inputs.size(); ++i) {
      SCOPED_TRACE(i);
      const auto opacity = ReferenceOpacity(inputs[i]);
      for (auto c = 0U; c < 3U; ++c)
        EXPECT_NEAR(std::bit_cast<float>(result[i * 8U + c]),
          inputs[i].environment.fog.fog_inscattering_luminance_rgb[c] * opacity,
          2.0e-5);
      EXPECT_NEAR(
        std::bit_cast<float>(result[i * 8U + 3U]), 1.0 - opacity, 2.0e-5);
      EXPECT_NEAR(std::bit_cast<float>(result[i * 8U + 4U]),
        Distance(inputs[i]), Distance(inputs[i]) * 2.0e-7);
    }
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, HeightFogCaptureAndVisibleParticipationAreIndependent)
  {
    auto inputs = std::array<FogInput, 8> {};
    inputs.fill(BaseFog());
    inputs[1].mode = 1U;
    inputs[2].environment.fog.flags &= ~kGpuFogFlagRenderInMainPass;
    inputs[3] = inputs[2];
    inputs[3].mode = 1U;
    inputs[4].environment.fog.flags &= ~kGpuFogFlagVisibleInRealTimeSkyCaptures;
    inputs[4].mode = 1U;
    inputs[5] = inputs[4];
    inputs[5].mode = 0U;
    inputs[6].view.flags = 0U;
    inputs[7] = inputs[6];
    inputs[7].mode = 1U;
    const auto values = Decode({ .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(FogInput),
      .record_kind = 34U,
      .decoded_words = 8U,
      .count = 8U });
    for (auto i = 0U; i < inputs.size(); ++i) {
      SCOPED_TRACE(i);
      const bool enabled = i != 2U && i != 4U && i != 6U;
      for (auto c = 0U; c < 4U; ++c) {
        const auto expected
          = enabled ? std::bit_cast<float>(values[c]) : (c == 3U ? 1.0F : 0.0F);
        EXPECT_FLOAT_EQ(std::bit_cast<float>(values[i * 8U + c]), expected);
      }
    }
  }

  NOLINT_TEST_F(LightingGpuAbiTest,
    HeightFogBothLightsSurviveHiddenDisksAndDisabledAtmosphere)
  {
    auto inputs = std::array<FogInput, 3> {};
    inputs.fill(BaseFog());
    for (auto& input : inputs) {
      input.environment.fog.flags |= kGpuFogFlagDirectionalInscattering;
      input.environment.fog.directional_exponent = 4.0F;
      input.environment.fog.fog_inscattering_luminance_rgb = {};
      input.view.atmosphere_light0_direction_angular_size = { 1, 0, 0, 0.01F };
      input.view.atmosphere_light1_direction_angular_size = { 1, 0, 0, 0.02F };
      input.view.height_fog_light0_illuminance_enabled = { 200, 400, 600, 1 };
    }
    inputs[1].view.height_fog_light1_illuminance_enabled = { 70, 60, 50, 1 };
    inputs[2] = inputs[1];
    inputs[2].view.atmosphere_light0_disk_luminance_rgb = { 200, 400, 600, 1 };
    inputs[2].view.atmosphere_light1_disk_luminance_rgb = { 70, 60, 50, 1 };
    const auto values = Decode({ .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(FogInput),
      .record_kind = 34U,
      .decoded_words = 8U,
      .count = 3U });
    for (auto c = 0U; c < 3U; ++c) {
      EXPECT_GT(std::bit_cast<float>(values[c]), 0.0F);
      EXPECT_GT(
        std::bit_cast<float>(values[8U + c]), std::bit_cast<float>(values[c]));
      EXPECT_FLOAT_EQ(std::bit_cast<float>(values[8U + c]),
        std::bit_cast<float>(values[16U + c]));
    }
  }
} // namespace
} // namespace oxygen::vortex::testing
