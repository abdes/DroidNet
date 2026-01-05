//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <glm/mat4x4.hpp>

#include <Oxygen/Scene/Light/LightCommon.h>

namespace oxygen::engine {

//! GPU-facing "cold" directional light shadow payload.
/*!\brief Layout mirrors HLSL struct `DirectionalLightShadows`.

This type is designed for `StructuredBuffer<DirectionalLightShadows>` uploads.
It contains per-cascade view-projection matrices for cascaded shadow mapping.

@note The cascade count is fixed by `oxygen::scene::kMaxShadowCascades`.
*/
struct alignas(16) DirectionalLightShadows {
  uint32_t cascade_count { oxygen::scene::kMaxShadowCascades };
  float distribution_exponent { 1.0F };
  float _pad0 { 0.0F };
  float _pad1 { 0.0F };

  std::array<float, oxygen::scene::kMaxShadowCascades> cascade_distances {};
  std::array<glm::mat4, oxygen::scene::kMaxShadowCascades> cascade_view_proj {};
};
static_assert(sizeof(DirectionalLightShadows) % 16 == 0,
  "DirectionalLightShadows size must be 16-byte aligned");
static_assert(sizeof(DirectionalLightShadows) == 288,
  "DirectionalLightShadows size must match HLSL packing");

} // namespace oxygen::engine
