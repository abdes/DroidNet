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

//! GPU-facing directional shadow metadata payload.
/*!\brief Layout mirrors HLSL struct `DirectionalShadowMetadata`.

This type is designed for `StructuredBuffer<DirectionalShadowMetadata>`
uploads. It contains directional shadow metadata, currently matching the
conventional cascaded-shadow-map path without baking that implementation name
into the engine contract.

@note The cascade count is fixed by `oxygen::scene::kMaxShadowCascades`.
*/
struct alignas(16) DirectionalShadowMetadata {
  std::uint32_t shadow_instance_index { 0U };
  std::uint32_t implementation_kind { 0U };
  float constant_bias { 0.0F };
  float normal_bias { 0.0F };

  std::uint32_t cascade_count { oxygen::scene::kMaxShadowCascades };
  std::uint32_t flags { 0U };
  float distribution_exponent { 1.0F };
  std::uint32_t resource_index { 0U };

  std::array<float, oxygen::scene::kMaxShadowCascades> cascade_distances {};
  std::array<glm::mat4, oxygen::scene::kMaxShadowCascades> cascade_view_proj {};
};
static_assert(sizeof(DirectionalShadowMetadata) % 16 == 0,
  "DirectionalShadowMetadata size must be 16-byte aligned");
static_assert(sizeof(DirectionalShadowMetadata) == 304,
  "DirectionalShadowMetadata size must match HLSL packing");

} // namespace oxygen::engine
