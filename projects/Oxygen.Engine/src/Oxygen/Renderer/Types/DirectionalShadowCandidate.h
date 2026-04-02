//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::engine {

//! CPU-side directional shadow candidate collected by LightManager.
/*!
 ShadowManager consumes these candidates to build stable per-view shadow
 products without introducing a second light collector.

 `node_handle` intentionally preserves the source scene-node identity for
 downstream VSM orchestration. The handle remains a scene-owned value object;
 this type does not move ownership into `SceneNodeImpl`.
*/
struct DirectionalShadowCandidate {
  scene::NodeHandle node_handle {};
  std::uint32_t light_index { 0U };
  std::uint32_t light_flags { 0U };
  std::uint32_t mobility { 0U };
  std::uint32_t resolution_hint { 0U };
  glm::vec3 direction_ws { 0.0F, 0.0F, -1.0F };
  glm::vec3 basis_up_ws { 0.0F, 0.0F, 1.0F };
  float bias { 0.0F };
  float normal_bias { 0.0F };
  std::uint32_t cascade_count { scene::kMaxShadowCascades };
  std::uint32_t split_mode { static_cast<std::uint32_t>(
    scene::DirectionalCsmSplitMode::kGenerated) };
  float max_shadow_distance { scene::kDefaultDirectionalMaxShadowDistance };
  float distribution_exponent {
    scene::kDefaultDirectionalDistributionExponent
  };
  float transition_fraction { scene::kDefaultDirectionalTransitionFraction };
  float distance_fadeout_fraction {
    scene::kDefaultDirectionalDistanceFadeoutFraction
  };
  std::array<float, scene::kMaxShadowCascades> cascade_distances {};
};

} // namespace oxygen::engine
