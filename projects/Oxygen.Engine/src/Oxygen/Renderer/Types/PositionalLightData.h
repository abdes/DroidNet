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

namespace oxygen::engine {

//! Light type encoded into `PositionalLightData::flags` bits [1:0].
enum class PositionalLightType : uint32_t {
  kPoint = 0u,
  kSpot = 1u,
};

//! Flags for GPU-facing positional lights (point/spot).
/*!\brief Bit values used by `PositionalLightData::flags`.

Bit layout (uint32_t):

- bits[1:0]: light_type (see PositionalLightType)
- bit2: affects_world
- bit3: casts_shadows
- bit4: contact_shadows
*/
enum class PositionalLightFlags : uint32_t {
  kNone = 0u,
  kAffectsWorld = OXYGEN_FLAG(2),
  kCastsShadows = OXYGEN_FLAG(3),
  kContactShadows = OXYGEN_FLAG(4),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(PositionalLightFlags)

inline constexpr uint32_t kPositionalLightTypeShift = 0u;
inline constexpr uint32_t kPositionalLightTypeMask = 0x3u
  << kPositionalLightTypeShift;

[[nodiscard]] inline constexpr auto PackPositionalLightType(
  const PositionalLightType type) noexcept -> uint32_t
{
  return (static_cast<uint32_t>(type) << kPositionalLightTypeShift)
    & kPositionalLightTypeMask;
}

//! GPU-facing payload for point and spot lights (local/positional lights).
/*!\brief Layout mirrors HLSL struct `PositionalLightData` (112 bytes).

This type is designed for `StructuredBuffer<PositionalLightData>` uploads.
The payload is intentionally self-contained so shaders can evaluate both point
and spot lights with a single element stride.

Packing rules:
- Fields are ordered to match HLSL 16-byte register packing.
- Explicit padding is included to ensure the size is a multiple of 16 bytes.
*/
struct alignas(16) PositionalLightData {
  // Register 0
  glm::vec3 position_ws { 0.0F, 0.0F, 0.0F };
  float range { 0.0F };

  // Register 1
  glm::vec3 color_rgb { 1.0F, 1.0F, 1.0F };
  float intensity { 1.0F };

  // Register 2
  glm::vec3 direction_ws { 0.0F, 0.0F, -1.0F };
  // Packed flags. See PositionalLightType and PositionalLightFlags.
  uint32_t flags { 0 };

  // Register 3
  float inner_cone_cos { 0.0F };
  float outer_cone_cos { 0.0F };
  float source_radius { 0.0F };
  float decay_exponent { 2.0F };

  // Register 4
  uint32_t attenuation_model { 0 };
  uint32_t mobility { 0 };
  uint32_t shadow_resolution_hint { 0 };
  uint32_t shadow_flags { 0 };

  // Register 5
  float shadow_bias { 0.0F };
  float shadow_normal_bias { 0.0F };
  float exposure_compensation_ev { 0.0F };
  float _pad0 { 0.0F };

  // Register 6
  uint32_t shadow_map_index { 0 };
  uint32_t _pad1 { 0 };
  uint32_t _pad2 { 0 };
  uint32_t _pad3 { 0 };
};
static_assert(sizeof(PositionalLightData) == 112,
  "PositionalLightData size must match HLSL packing");
static_assert(sizeof(PositionalLightData) % 16 == 0,
  "PositionalLightData size must be 16-byte aligned");

} // namespace oxygen::engine
