//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>

namespace oxygen::engine {

//! Light type encoded into `PositionalLightData::flags` bits [1:0].
enum class PositionalLightType : uint32_t { // NOLINT(*enum-size)
  kPoint = 0,
  kSpot = 1,
};

//! Flags for GPU-facing positional lights (point/spot).
/*!
 @note bits[1:0]: light_type (see PositionalLightType)
*/
enum class PositionalLightFlags : uint32_t { // NOLINT(*enum-size)
  // 0 - reserved for light type
  // 1 - reserved for light type
  kAffectsWorld = OXYGEN_FLAG(2),
  kCastsShadows = OXYGEN_FLAG(3),
  kContactShadows = OXYGEN_FLAG(4),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(PositionalLightFlags)

inline constexpr auto kPositionalLightTypeMask
  = static_cast<PositionalLightFlags>(0b11);

[[nodiscard]] constexpr auto PackPositionalLightType(
  const PositionalLightType type) noexcept -> PositionalLightFlags
{
  return static_cast<PositionalLightFlags>(static_cast<uint32_t>(type))
    & kPositionalLightTypeMask;
}

//! GPU-facing payload for point and spot lights (local/positional lights).
/*!
 This type is designed for `StructuredBuffer<PositionalLightData>` uploads.
 The payload is intentionally self-contained so shaders can evaluate both point
 and spot lights with a single element stride.

 Packing rules:
 - Fields are ordered to match HLSL 16-byte register packing.
 - Explicit padding is included to ensure the size is a multiple of 16 bytes.
*/
struct alignas(packing::kShaderDataFieldAlignment) PositionalLightData {
  // Register 0
  glm::vec3 position_ws { 0.0F, 0.0F, 0.0F };
  float range { scene::PointLight::kDefaultRange };

  // Register 1
  glm::vec3 color_rgb { 1.0F, 1.0F, 1.0F };
  float luminous_flux_lm { scene::PointLight::kDefaultLuminousFluxLm };

  // Register 2
  glm::vec3 direction_ws { 0.0F, 0.0F, -1.0F };
  // Packed flags. See PositionalLightType and PositionalLightFlags.
  uint32_t flags { 0 };

  // Register 3
  float inner_cone_cos { std::cos(scene::SpotLight::kDefaultInnerConeAngle) };
  float outer_cone_cos { std::cos(scene::SpotLight::kDefaultOuterConeAngle) };
  float source_radius { scene::PointLight::kDefaultSourceRadius };
  float decay_exponent { scene::PointLight::kDefaultDecayExponent };

  // Register 4
  uint32_t attenuation_model { 0 };
  uint32_t mobility { 0 };
  uint32_t shadow_resolution_hint { 0 };
  uint32_t shadow_flags { 0 };

  // Register 5
  float shadow_bias { 0.0F };
  float shadow_normal_bias { 0.0F };
  float exposure_compensation_ev { 0.0F };
  uint32_t shadow_map_index { 0 };
};
static_assert(
  sizeof(PositionalLightData) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::engine
