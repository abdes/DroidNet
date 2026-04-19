//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/vec3.hpp>

namespace oxygen::vortex::environment {

struct VolumetricFogModel {
  bool enabled { false };
  float scattering_distribution { 0.0F };
  glm::vec3 albedo { 1.0F, 1.0F, 1.0F };
  glm::vec3 emissive { 0.0F, 0.0F, 0.0F };
  float extinction_scale { 1.0F };
  float distance { 0.0F };
  float start_distance { 0.0F };
  float near_fade_in_distance { 0.0F };
  float static_lighting_scattering_intensity { 1.0F };
  bool override_light_colors_with_fog_inscattering_colors { false };
};

} // namespace oxygen::vortex::environment
