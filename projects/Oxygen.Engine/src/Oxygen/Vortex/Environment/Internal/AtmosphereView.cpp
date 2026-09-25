//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereView.h>

namespace oxygen::vortex::environment::internal {
namespace {
  constexpr std::uint32_t kEnvironmentViewFlagAtmosphereEnabled = 1U;
  constexpr std::uint32_t kEnvironmentViewFlagReflectionCapture = 2U;
  auto ResolvePlanetCenterWs(const environment::AtmosphereModel& atmosphere)
    -> glm::vec3
  {
    switch (atmosphere.transform_mode) {
    case environment::AtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin:
      return { 0.0F, 0.0F, -atmosphere.planet_radius_m };
    case environment::AtmosphereTransformMode::kPlanetTopAtComponentTransform:
      return atmosphere.planet_anchor_position_ws
        + glm::vec3 { 0.0F, 0.0F, -atmosphere.planet_radius_m };
    case environment::AtmosphereTransformMode::
      kPlanetCenterAtComponentTransform:
      return atmosphere.planet_anchor_position_ws;
    default:
      return { 0.0F, 0.0F, -atmosphere.planet_radius_m };
    }
  }

  auto SafeNormalizeOrFallback(const glm::vec3 value, const glm::vec3 fallback)
    -> glm::vec3
  {
    const auto length_sq = glm::dot(value, value);
    if (length_sq <= 1.0e-8F) {
      return fallback;
    }
    return glm::normalize(value);
  }

  auto MetersToSkyUnitVec3(const glm::vec3 meters) -> glm::vec3
  {
    return meters * engine::atmos::kMToSkyUnit;
  }

  //! Builds the shared sky-view local basis used by both the LUT producer and
  //! the main-view sky consumer.
  /*!
   Contract:
   - rows are expressed in Oxygen world space
   - row0 = local +X = sun/physics hint projected onto the tangent plane
   - row1 = local +Y = right = cross(up, forward)
   - row2 = local +Z = up

   This basis must stay right-handed and must not silently switch to "left".
   The sky-view LUT parameterization already applies its own azimuth convention
   in shader code, so mirroring this basis would not "fix" sun position; it
   would only create a mirrored local frame shared by producer and consumer.
  */
  auto BuildSkyViewReferentialRows(const glm::vec3 up,
    const glm::vec3 forward_hint) -> std::array<glm::vec4, 3>
  {
    const auto safe_up
      = SafeNormalizeOrFallback(up, engine::atmos::kDefaultPlanetUp);
    // The sky-view referential is expressed in Oxygen world space, not view
    // space. Keep its fallback axes on the engine world basis: Z-up,
    // -Y-forward.
    auto forward = SafeNormalizeOrFallback(forward_hint, space::move::Forward);
    // +Y in the local sky-view frame is RIGHT, not LEFT. Using cross(forward,
    // up) would mirror the basis horizontally under Oxygen's right-handed Z-up
    // law.
    auto right = glm::cross(safe_up, forward);
    const auto dot_main = std::abs(glm::dot(safe_up, forward));
    if (dot_main > 0.999F || glm::dot(right, right) <= 1.0e-8F) {
      right = glm::cross(safe_up, glm::vec3(space::move::Forward));
      right = SafeNormalizeOrFallback(right, glm::vec3(space::move::Right));
      forward = SafeNormalizeOrFallback(
        glm::cross(right, safe_up), glm::vec3(space::move::Forward));
    } else {
      right = SafeNormalizeOrFallback(right, glm::vec3(space::move::Right));
      forward = SafeNormalizeOrFallback(glm::cross(right, safe_up), forward);
    }

    return {
      glm::vec4(forward, 0.0F),
      glm::vec4(right, 0.0F),
      glm::vec4(safe_up, 0.0F),
    };
  }

  auto ComputeSunDiskLuminanceRgb(
    const environment::AtmosphereLightModel& light) -> glm::vec3
  {
    if (light.angular_size_radians == 0.0F)
      return glm::vec3 { 0.0F };
    const double sine = std::sin(0.5 * light.angular_size_radians);
    const double projected_solid_angle = std::numbers::pi * sine * sine;
    return glm::vec3(glm::dvec3(light.disk_luminance_scale_rgb)
      * glm::dvec3(light.illuminance_rgb_lux) / projected_solid_angle);
  }

  auto ComputeHeightFogIlluminanceRgb(
    const environment::AtmosphereLightModel& light) -> glm::vec3
  {
    // Preserve the established finite-disk calibration. A zero-angle light
    // still supplies its authored illuminance to participating fog.
    auto calibration = 1.0;
    if (light.angular_size_radians > 0.0F) {
      const auto radius = 0.5 * light.angular_size_radians;
      const auto sine = std::sin(radius);
      const auto projected = std::numbers::pi * sine * sine;
      const auto cap = 2.0 * std::numbers::pi * (1.0 - std::cos(radius));
      calibration = std::max(cap, 1.0e-6) / projected;
    }
    return glm::vec3(glm::dvec3(light.disk_luminance_scale_rgb)
      * glm::dvec3(light.illuminance_rgb_lux) * calibration);
  }

} // namespace

auto ResolveSkyCaptureOrigin(const AtmosphereModel& atmosphere) -> glm::vec3
{
  const auto offset
    = engine::atmos::SkyUnitToMeters(engine::atmos::kPlanetRadiusOffsetKm);
  return ResolvePlanetCenterWs(atmosphere)
    + glm::vec3(0.0F, 0.0F, atmosphere.planet_radius_m + offset);
}

auto BuildAtmosphereViewData(const StableAtmosphereState& stable_state,
  const AtmosphereLutCache::InternalParameters& internal_params,
  const glm::vec3 camera_position, const bool with_height_fog,
  const bool reflection_capture) -> EnvironmentViewData
{
  const auto& atmosphere = stable_state.view_products.atmosphere;
  const auto planet_center_ws = ResolvePlanetCenterWs(atmosphere);
  const auto planet_center_translated_ws = planet_center_ws - camera_position;
  const auto camera_to_planet_translated_ws = -planet_center_translated_ws;
  const auto distance_to_planet_center_m
    = glm::length(camera_to_planet_translated_ws);
  const auto planet_radius_offset_m
    = engine::atmos::SkyUnitToMeters(engine::atmos::kPlanetRadiusOffsetKm);
  auto sky_camera_translated_world_origin = glm::vec3 { 0.0F, 0.0F, 0.0F };
  if (distance_to_planet_center_m
    < (atmosphere.planet_radius_m + planet_radius_offset_m)) {
    const auto direction = SafeNormalizeOrFallback(
      camera_to_planet_translated_ws, engine::atmos::kDefaultPlanetUp);
    sky_camera_translated_world_origin = planet_center_translated_ws
      + direction * (atmosphere.planet_radius_m + planet_radius_offset_m);
  }
  const auto sky_camera_planet_vector
    = sky_camera_translated_world_origin - planet_center_translated_ws;
  const auto planet_up_ws = SafeNormalizeOrFallback(
    sky_camera_planet_vector, engine::atmos::kDefaultPlanetUp);
  const auto view_height_m = glm::length(sky_camera_planet_vector);
  const auto camera_altitude_km = engine::atmos::MetersToSkyUnit(
    std::max(view_height_m - atmosphere.planet_radius_m, 0.0F));
  auto sun_direction_ws = engine::atmos::kDefaultSunDirection;
  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    const auto& slot0 = stable_state.view_products.atmosphere_lights[0];
    const auto length_sq
      = glm::dot(slot0.direction_to_light_ws, slot0.direction_to_light_ws);
    if (length_sq > 1.0e-6F) {
      sun_direction_ws = glm::normalize(slot0.direction_to_light_ws);
    }
  }
  const auto referential_rows
    = BuildSkyViewReferentialRows(planet_up_ws, sun_direction_ws);

  auto data = EnvironmentViewData {};
  data.flags = with_height_fog ? kEnvironmentViewFlagHeightFog : 0U;
  if (atmosphere.enabled) {
    data.flags |= kEnvironmentViewFlagAtmosphereEnabled;
  }
  if (reflection_capture) {
    data.flags |= kEnvironmentViewFlagReflectionCapture;
  }
  data.transform_mode = static_cast<std::uint32_t>(atmosphere.transform_mode);
  data.atmosphere_light_count
    = stable_state.view_products.atmosphere_light_count;
  data.sky_view_lut_slice = 0.0F;
  data.planet_to_sun_cos_zenith
    = glm::dot(glm::normalize(planet_up_ws), glm::normalize(sun_direction_ws));
  data.aerial_perspective_distance_scale
    = atmosphere.aerial_perspective_distance_scale;
  data.aerial_scattering_strength = atmosphere.aerial_scattering_strength;
  data.planet_center_ws_pad = glm::vec4(planet_center_ws, 0.0F);
  data.planet_up_ws_camera_altitude_km
    = glm::vec4(planet_up_ws, camera_altitude_km);
  data.sky_planet_translated_world_center_km_and_view_height_km
    = glm::vec4(MetersToSkyUnitVec3(planet_center_translated_ws),
      engine::atmos::MetersToSkyUnit(view_height_m));
  data.sky_camera_translated_world_origin_km_pad
    = glm::vec4(MetersToSkyUnitVec3(sky_camera_translated_world_origin), 0.0F);
  data.sky_view_lut_referential_row0 = referential_rows[0];
  data.sky_view_lut_referential_row1 = referential_rows[1];
  data.sky_view_lut_referential_row2 = referential_rows[2];
  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    const auto disk_luminance = atmosphere.sun_disk_enabled
      ? ComputeSunDiskLuminanceRgb(
          stable_state.view_products.atmosphere_lights[0])
      : glm::vec3 { 0.0F, 0.0F, 0.0F };
    data.atmosphere_light0_direction_angular_size = glm::vec4(
      stable_state.view_products.atmosphere_lights[0].direction_to_light_ws,
      0.5F
        * std::max(0.0F,
          stable_state.view_products.atmosphere_lights[0]
            .angular_size_radians));
    data.atmosphere_light0_disk_luminance_rgb
      = glm::vec4(disk_luminance, atmosphere.sun_disk_enabled ? 1.0F : 0.0F);
    data.height_fog_light0_illuminance_enabled
      = glm::vec4(ComputeHeightFogIlluminanceRgb(
                    stable_state.view_products.atmosphere_lights[0]),
        1.0F);
  }
  if (stable_state.view_products.atmosphere_lights[1].enabled) {
    const auto disk_luminance = atmosphere.sun_disk_enabled
      ? ComputeSunDiskLuminanceRgb(
          stable_state.view_products.atmosphere_lights[1])
      : glm::vec3 { 0.0F, 0.0F, 0.0F };
    data.atmosphere_light1_direction_angular_size = glm::vec4(
      stable_state.view_products.atmosphere_lights[1].direction_to_light_ws,
      0.5F
        * std::max(0.0F,
          stable_state.view_products.atmosphere_lights[1]
            .angular_size_radians));
    data.atmosphere_light1_disk_luminance_rgb
      = glm::vec4(disk_luminance, atmosphere.sun_disk_enabled ? 1.0F : 0.0F);
    data.height_fog_light1_illuminance_enabled
      = glm::vec4(ComputeHeightFogIlluminanceRgb(
                    stable_state.view_products.atmosphere_lights[1]),
        1.0F);
  }
  data.sky_luminance_factor_height_fog_contribution = glm::vec4(
    atmosphere.sky_luminance_factor_rgb, atmosphere.height_fog_contribution);
  data.sky_aerial_luminance_aerial_start_depth_km
    = glm::vec4(atmosphere.sky_and_aerial_perspective_luminance_factor_rgb,
      engine::atmos::MetersToSkyUnit(
        atmosphere.aerial_perspective_start_depth_m));
  data.trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass
    = glm::vec4(atmosphere.trace_sample_count_scale,
      atmosphere.transmittance_min_light_elevation_deg,
      atmosphere.holdout ? 1.0F : 0.0F,
      atmosphere.render_in_main_pass ? 1.0F : 0.0F);
  const auto depth_resolution = static_cast<float>(
    std::max(internal_params.camera_aerial_depth_resolution, 1U));
  const auto depth_slice_length_km
    = internal_params.camera_aerial_depth_slice_length_km;
  data.camera_aerial_volume_depth_params = glm::vec4(depth_resolution,
    1.0F / depth_resolution, depth_slice_length_km,
    depth_slice_length_km > 1.0e-6F ? 1.0F / depth_slice_length_km : 0.0F);
  return data;
}

} // namespace oxygen::vortex::environment::internal
