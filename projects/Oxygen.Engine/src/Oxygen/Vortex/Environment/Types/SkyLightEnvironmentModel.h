//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/vec3.hpp>

#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::vortex::environment {

inline constexpr std::uint32_t kSkyLightSourceCapturedScene = 0U;
inline constexpr std::uint32_t kSkyLightSourceSpecifiedCubemap = 1U;

struct SkyLightEnvironmentModel {
  bool enabled { false };
  std::uint32_t source { 0U };
  content::ResourceKey cubemap_resource {};
  float intensity_mul { 1.0F };
  glm::vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
  float diffuse_intensity { 1.0F };
  float specular_intensity { 1.0F };
  float source_cubemap_angle_radians { 0.0F };
  glm::vec3 lower_hemisphere_color { 0.0F, 0.0F, 0.0F };
  bool lower_hemisphere_is_solid_color { true };
  // Kept beside the other flag: the published view state admits no padding.
  bool affect_reflections { true };
  float lower_hemisphere_blend_alpha { 1.0F };
  float volumetric_scattering_intensity { 1.0F };
  //! Calibrated upward illuminance of the specified cubemap; zero is raw.
  float illuminance_lux { 0.0F };
};

} // namespace oxygen::vortex::environment
