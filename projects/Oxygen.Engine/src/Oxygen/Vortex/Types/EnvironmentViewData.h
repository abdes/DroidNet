//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/vec4.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Atmosphere.h>

namespace oxygen::vortex {

//! Environment-owned per-view atmosphere context and controls.
struct alignas(packing::kShaderDataFieldAlignment) EnvironmentViewData {
  static constexpr size_t kSize = 272;

  uint32_t flags { 0U };
  uint32_t transform_mode { 0U };
  uint32_t atmosphere_light_count { 0U };
  uint32_t _pad0 { 0U };
  float sky_view_lut_slice { 0.0F };
  float planet_to_sun_cos_zenith { 0.0F };
  float aerial_perspective_distance_scale {
    engine::atmos::kDefaultAerialPerspectiveDistanceScale
  };
  float aerial_scattering_strength {
    engine::atmos::kDefaultAerialScatteringStrength
  };
  glm::vec4 planet_center_ws_pad {
    0.0F,
    0.0F,
    -engine::atmos::kDefaultPlanetRadiusM,
    0.0F,
  };
  glm::vec4 planet_up_ws_camera_altitude_km {
    engine::atmos::kDefaultPlanetUp,
    0.0F,
  };
  glm::vec4 sky_planet_translated_world_center_km_and_view_height_km {
    0.0F,
    0.0F,
    -engine::atmos::kDefaultPlanetRadiusKm,
    engine::atmos::kDefaultPlanetRadiusKm,
  };
  glm::vec4 sky_camera_translated_world_origin_km_pad {
    0.0F,
    0.0F,
    0.0F,
    0.0F,
  };
  glm::vec4 sky_view_lut_referential_row0 {
    1.0F,
    0.0F,
    0.0F,
    0.0F,
  };
  glm::vec4 sky_view_lut_referential_row1 {
    0.0F,
    1.0F,
    0.0F,
    0.0F,
  };
  glm::vec4 sky_view_lut_referential_row2 {
    0.0F,
    0.0F,
    1.0F,
    0.0F,
  };
  glm::vec4 atmosphere_light0_direction_angular_size {
    0.0F,
    0.0F,
    1.0F,
    0.0F,
  };
  glm::vec4 atmosphere_light0_disk_luminance_rgb {
    0.0F,
    0.0F,
    0.0F,
    0.0F,
  };
  glm::vec4 sky_luminance_factor_height_fog_contribution {
    1.0F,
    1.0F,
    1.0F,
    1.0F,
  };
  glm::vec4 atmosphere_light1_direction_angular_size {
    0.0F,
    0.0F,
    1.0F,
    0.0F,
  };
  glm::vec4 atmosphere_light1_disk_luminance_rgb {
    0.0F,
    0.0F,
    0.0F,
    0.0F,
  };
  glm::vec4 sky_aerial_luminance_aerial_start_depth_km {
    1.0F,
    1.0F,
    1.0F,
    engine::atmos::MetersToSkyUnit(100.0F),
  };
  glm::vec4
    trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass {
      1.0F,
      -90.0F,
      0.0F,
      1.0F,
    };
  glm::vec4 camera_aerial_volume_depth_params {
    16.0F,
    1.0F / 16.0F,
    6.0F,
    1.0F / 6.0F,
  };
};

static_assert(sizeof(EnvironmentViewData) == EnvironmentViewData::kSize);
static_assert(
  alignof(EnvironmentViewData) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(EnvironmentViewData) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::vortex
