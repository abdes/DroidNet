//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/vec3.hpp>

#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::vortex::environment {

struct SkyLightEnvironmentModel {
  bool enabled { false };
  std::uint32_t source { 0U };
  content::ResourceKey cubemap_resource {};
  float intensity_mul { 1.0F };
  glm::vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
  float diffuse_intensity { 1.0F };
  float specular_intensity { 1.0F };
  bool real_time_capture_enabled { false };
  glm::vec3 lower_hemisphere_color { 0.0F, 0.0F, 0.0F };
  float volumetric_scattering_intensity { 1.0F };
  bool affect_reflections { true };
  bool affect_global_illumination { true };
};

} // namespace oxygen::vortex::environment
