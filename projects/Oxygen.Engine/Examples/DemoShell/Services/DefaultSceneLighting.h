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

//! Authored sun bias shared by example-created lights and preview lighting.
inline constexpr float kDefaultDemoSunShadowBias = 0.03F;

//! Authored default lighting/environment for procedural demo scenes.
struct DefaultSceneLightingDesc {
  std::string_view sun_node_name { "SunLight" };
  glm::vec3 sun_position { -10.0F, 10.0F, 16.0F };
  glm::vec3 focus_point { 0.0F, 0.0F, 1.0F };
  glm::vec3 sun_color_rgb { 1.0F, 0.97F, 0.92F };
  float sun_intensity_lux { 100000.0F };
  float sun_source_angle_degrees { 0.53F };
  float sun_shadow_bias { kDefaultDemoSunShadowBias };
  bool casts_shadows { true };
};

auto EnsureDefaultSceneLighting(scene::Scene& scene,
  const DefaultSceneLightingDesc& desc = {}) -> scene::SceneNode;

//! Creates a caller-owned preview sun without modifying the environment.
//!
//! The caller must resolve any existing directional-light roles first.
auto CreatePreviewSun(scene::Scene& scene,
  const DefaultSceneLightingDesc& desc = {}) -> scene::SceneNode;

//! Adds a preview sun only if no directional component exists in the scene.
/*!
 Disabled and invisible directional lights also suppress creation. Existing

 * lights and environment systems are never modified. Returns the newly created

 * node, or an invalid node when a directional light already exists. The caller

 * owns the preview policy and must exclude placeholder scenes as
 * appropriate.
*/
auto AddPreviewSunIfMissing(scene::Scene& scene,
  const DefaultSceneLightingDesc& desc = {}) -> scene::SceneNode;

} // namespace oxygen::examples
