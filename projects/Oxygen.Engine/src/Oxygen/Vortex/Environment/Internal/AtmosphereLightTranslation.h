//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h>

namespace oxygen::vortex::environment::internal {

namespace detail {

  inline constexpr float kGroundObserverHeightOffsetM = 500.0F;
  inline constexpr std::uint32_t kGroundTransmittanceSampleCount = 15U;

  inline auto SafeNormalizeOrFallback(
    const glm::vec3& value, const glm::vec3& fallback) -> glm::vec3
  {
    const auto length_sq = glm::dot(value, value);
    if (length_sq <= 1.0e-8F) {
      return fallback;
    }
    return value / std::sqrt(length_sq);
  }

  inline auto RaySphereIntersectFarthestPositive(
    const glm::vec3& origin, const glm::vec3& direction, const float radius)
    -> float
  {
    const auto a = glm::dot(direction, direction);
    const auto b = 2.0F * glm::dot(origin, direction);
    const auto c = glm::dot(origin, origin) - radius * radius;
    const auto discriminant = b * b - 4.0F * a * c;
    if (discriminant < 0.0F || a <= 1.0e-8F) {
      return -1.0F;
    }

    const auto sqrt_discriminant = std::sqrt(discriminant);
    const auto denominator = 2.0F * a;
    const auto t0 = (-b - sqrt_discriminant) / denominator;
    const auto t1 = (-b + sqrt_discriminant) / denominator;
    if (t0 < 0.0F && t1 < 0.0F) {
      return -1.0F;
    }
    return std::max(t0, t1);
  }

  inline auto AtmosphereExponentialDensity(
    const float altitude_m, const float scale_height_m) -> float
  {
    return std::exp(-std::max(altitude_m, 0.0F) / std::max(scale_height_m, 1.0e-4F));
  }

  inline auto EvaluateDensityProfile(
    const float altitude_m, const engine::atmos::DensityProfile& profile) -> float
  {
    const auto height_m = std::max(altitude_m, 0.0F);
    auto layer = profile.layers[1];
    if (height_m < profile.layers[0].width_m) {
      layer = profile.layers[0];
    }

    if (layer.exp_term != 0.0F) {
      return layer.exp_term * std::exp(layer.linear_term * height_m)
        + layer.constant_term;
    }

    return layer.linear_term * height_m + layer.constant_term;
  }

  inline auto OzoneAbsorptionDensity(
    const float altitude_m, const engine::atmos::DensityProfile& profile) -> float
  {
    return std::clamp(EvaluateDensityProfile(altitude_m, profile), 0.0F, 1.0F);
  }

} // namespace detail

inline auto ComputeGroundTransmittanceTowardLight(
  const scene::environment::SkyAtmosphere& atmosphere,
  const glm::vec3& direction_to_light_ws) -> glm::vec3
{
  if (!atmosphere.IsEnabled()) {
    return { 1.0F, 1.0F, 1.0F };
  }

  const auto planet_radius_m = atmosphere.GetPlanetRadiusMeters();
  const auto atmosphere_height_m = atmosphere.GetAtmosphereHeightMeters();
  const auto top_radius_m = planet_radius_m + atmosphere_height_m;
  const auto min_elevation_radians = glm::radians(
    atmosphere.GetTransmittanceMinLightElevationDeg());
  const auto light_direction = detail::SafeNormalizeOrFallback(
    direction_to_light_ws, engine::atmos::kDefaultSunDirection);
  const auto clamped_elevation_radians = std::max(
    std::asin(std::clamp(light_direction.z, -1.0F, 1.0F)),
    min_elevation_radians);
  const auto ray_direction = glm::vec3 {
    std::cos(clamped_elevation_radians),
    0.0F,
    std::sin(clamped_elevation_radians),
  };
  const auto ray_origin = glm::vec3 {
    0.0F,
    0.0F,
    planet_radius_m + detail::kGroundObserverHeightOffsetM,
  };

  const auto ray_length = detail::RaySphereIntersectFarthestPositive(
    ray_origin, ray_direction, top_radius_m);
  if (ray_length <= 1.0e-4F) {
    return { 1.0F, 1.0F, 1.0F };
  }

  const auto step_count = static_cast<float>(detail::kGroundTransmittanceSampleCount);
  const auto step_size = ray_length / step_count;
  auto optical_depth_rayleigh = 0.0F;
  auto optical_depth_mie = 0.0F;
  auto optical_depth_absorption = 0.0F;

  for (std::uint32_t sample_index = 0U;
       sample_index < detail::kGroundTransmittanceSampleCount; ++sample_index) {
    const auto distance = (static_cast<float>(sample_index) + 0.5F) * step_size;
    const auto sample_position = ray_origin + ray_direction * distance;
    const auto altitude_m
      = std::max(glm::length(sample_position) - planet_radius_m, 0.0F);
    optical_depth_rayleigh += detail::AtmosphereExponentialDensity(
      altitude_m, atmosphere.GetRayleighScaleHeightMeters()) * step_size;
    optical_depth_mie += detail::AtmosphereExponentialDensity(
      altitude_m, atmosphere.GetMieScaleHeightMeters()) * step_size;
    optical_depth_absorption += detail::OzoneAbsorptionDensity(
      altitude_m, atmosphere.GetOzoneDensityProfile()) * step_size;
  }

  const auto extinction = atmosphere.GetRayleighScatteringRgb()
      * optical_depth_rayleigh
    + (atmosphere.GetMieScatteringRgb() + atmosphere.GetMieAbsorptionRgb())
        * optical_depth_mie
    + atmosphere.GetAbsorptionRgb() * optical_depth_absorption;
  return glm::exp(-extinction);
}

inline auto BuildAtmosphereLightModel(
  const scene::ResolvedDirectionalLightView& resolved, const std::uint32_t slot_index,
  const scene::environment::SkyAtmosphere* atmosphere) -> AtmosphereLightModel
{
  auto model = AtmosphereLightModel {};
  model.enabled = true;
  model.use_per_pixel_transmittance
    = resolved.Light().GetUsePerPixelAtmosphereTransmittance();
  model.slot_index = slot_index;
  model.direction_to_light_ws = detail::SafeNormalizeOrFallback(
    resolved.DirectionToLightWs(), engine::atmos::kDefaultSunDirection);
  model.angular_size_radians = resolved.Light().GetAngularSizeRadians();
  model.illuminance_rgb_lux
    = resolved.Light().Common().color_rgb * resolved.Light().GetIntensityLux();
  model.illuminance_lux = resolved.Light().GetIntensityLux();
  model.disk_luminance_scale_rgba
    = resolved.Light().GetAtmosphereDiskLuminanceScale();
  model.transmittance_toward_sun_rgb
    = atmosphere != nullptr
        ? ComputeGroundTransmittanceTowardLight(
            *atmosphere, model.direction_to_light_ws)
        : glm::vec3 { 1.0F, 1.0F, 1.0F };
  if (model.use_per_pixel_transmittance) {
    model.direct_light_authority_flags
      |= kAtmosphereDirectLightFlagPerPixelTransmittance;
  } else if (atmosphere != nullptr && atmosphere->IsEnabled()) {
    model.direct_light_authority_flags
      |= kAtmosphereDirectLightFlagHasBakedGroundTransmittance;
  }
  return model;
}

} // namespace oxygen::vortex::environment::internal
