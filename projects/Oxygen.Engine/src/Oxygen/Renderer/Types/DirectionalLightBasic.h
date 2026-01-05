//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Flags for GPU-facing directional lights.
/*!\brief Bit values used by `DirectionalLightBasic::flags`.

Bit layout (uint32_t):

- bit0: affects_world
- bit1: casts_shadows
- bit2: contact_shadows
- bit3: environment_contribution
*/
enum class DirectionalLightFlags : uint32_t {
  kNone = 0u,
  kAffectsWorld = OXYGEN_FLAG(0),
  kCastsShadows = OXYGEN_FLAG(1),
  kContactShadows = OXYGEN_FLAG(2),
  kEnvironmentContribution = OXYGEN_FLAG(3),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(DirectionalLightFlags)

//! GPU-facing "hot" directional light parameters.
/*!\brief Layout mirrors HLSL struct `DirectionalLightBasic`.

This type is designed for `StructuredBuffer<DirectionalLightBasic>` uploads.
All fields are world-space and follow scene conventions:

- `direction_ws` is the incoming ray direction (light -> scene).

The struct is padded to a 16-byte multiple for predictable HLSL packing.
*/
struct alignas(16) DirectionalLightBasic {
  glm::vec3 color_rgb { 1.0F, 1.0F, 1.0F };
  float intensity { 1.0F };
  glm::vec3 direction_ws { 0.0F, -1.0F, 0.0F };
  float angular_size_radians { 0.0F };

  // Indices/flags are kept in a 16-byte register for predictable packing.
  uint32_t shadow_index { 0 };
  // Bitmask; see DirectionalLightFlags.
  uint32_t flags { 0 };
  uint32_t _pad0 { 0 };
  uint32_t _pad1 { 0 };
};
static_assert(sizeof(DirectionalLightBasic) % 16 == 0,
  "DirectionalLightBasic size must be 16-byte aligned");
static_assert(sizeof(DirectionalLightBasic) == 48,
  "DirectionalLightBasic size must match HLSL packing");

} // namespace oxygen::engine
