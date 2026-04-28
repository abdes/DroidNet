//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <glm/vec3.hpp>

#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::examples {

//! Authored default lighting/environment for procedural demo scenes.
struct DefaultSceneLightingDesc {
  std::string_view sun_node_name { "SunLight" };
  glm::vec3 sun_position { -10.0F, 10.0F, 16.0F };
  glm::vec3 focus_point { 0.0F, 0.0F, 1.0F };
  glm::vec3 sun_color_rgb { 1.0F, 0.97F, 0.92F };
  float sun_intensity_lux { 100000.0F };
  float sun_source_angle_degrees { 0.53F };
  bool casts_shadows { true };
};

auto EnsureDefaultSceneLighting(
  scene::Scene& scene, const DefaultSceneLightingDesc& desc = {})
  -> scene::SceneNode;

} // namespace oxygen::examples
