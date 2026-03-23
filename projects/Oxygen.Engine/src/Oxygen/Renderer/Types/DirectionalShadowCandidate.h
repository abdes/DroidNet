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

namespace oxygen::engine {

//! CPU-side directional shadow candidate collected by LightManager.
/*!
 ShadowManager consumes these candidates to build stable per-view shadow
 products without introducing a second light collector.
*/
struct DirectionalShadowCandidate {
  std::uint32_t light_index { 0U };
  std::uint32_t light_flags { 0U };
  std::uint32_t mobility { 0U };
  std::uint32_t resolution_hint { 0U };
  glm::vec3 direction_ws { 0.0F, 0.0F, -1.0F };
  glm::vec3 basis_up_ws { 0.0F, 0.0F, 1.0F };
  float bias { 0.0F };
  float normal_bias { 0.0F };
  std::uint32_t cascade_count { scene::kMaxShadowCascades };
  float distribution_exponent { 1.0F };
  std::array<float, scene::kMaxShadowCascades> cascade_distances {};
};

} // namespace oxygen::engine
