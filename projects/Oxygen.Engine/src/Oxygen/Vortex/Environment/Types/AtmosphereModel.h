//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Core/Types/Atmosphere.h>

namespace oxygen::vortex::environment {

enum class AtmosphereTransformMode : std::uint32_t {
  kPlanetTopAtAbsoluteWorldOrigin = 0,
  kPlanetTopAtComponentTransform = 1,
  kPlanetCenterAtComponentTransform = 2,
};

struct AtmosphereModel {
  bool enabled { false };
  AtmosphereTransformMode transform_mode {
    AtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin
  };
  float planet_radius_m { engine::atmos::kDefaultPlanetRadiusM };
  float atmosphere_height_m { engine::atmos::kDefaultAtmosphereHeightM };
  glm::vec3 ground_albedo_rgb { 0.1F, 0.1F, 0.1F };
  glm::vec3 rayleigh_scattering_rgb {
    engine::atmos::kDefaultRayleighScatteringRgb
  };
  float rayleigh_scale_height_m { engine::atmos::kDefaultRayleighScaleHeightM };
  glm::vec3 mie_scattering_rgb { engine::atmos::kDefaultMieScatteringRgb };
  glm::vec3 mie_absorption_rgb { engine::atmos::kDefaultMieAbsorptionRgb };
  float mie_scale_height_m { engine::atmos::kDefaultMieScaleHeightM };
  float mie_anisotropy { engine::atmos::kDefaultMieAnisotropyG };
  glm::vec3 ozone_absorption_rgb { engine::atmos::kDefaultOzoneAbsorptionRgb };
  engine::atmos::DensityProfile ozone_density_profile {
    engine::atmos::kDefaultOzoneDensityProfile
  };
  float multi_scattering_factor { 1.0F };
  glm::vec3 sky_luminance_factor_rgb { 1.0F, 1.0F, 1.0F };
  glm::vec3 sky_and_aerial_perspective_luminance_factor_rgb {
    1.0F,
    1.0F,
    1.0F,
  };
  float aerial_perspective_distance_scale {
    engine::atmos::kDefaultAerialPerspectiveDistanceScale
  };
  float aerial_scattering_strength {
    engine::atmos::kDefaultAerialScatteringStrength
  };
  float aerial_perspective_start_depth_m { 100.0F };
  float height_fog_contribution { 1.0F };
  float trace_sample_count_scale { 1.0F };
  float transmittance_min_light_elevation_deg { -6.0F };
  bool sun_disk_enabled { true };
  bool holdout { false };
  bool render_in_main_pass { true };
};

} // namespace oxygen::vortex::environment
