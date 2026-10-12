//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h>

namespace oxygen::vortex::environment::internal {

//! A vector and a fourth component as one HLSL `float4`.
[[nodiscard]] inline auto PackFloat4(
  const glm::vec3 value, const float w = 0.0F) -> std::array<float, 4>
{
  return { value.x, value.y, value.z, w };
}

//! One atmosphere light as the two `float4` rows the atmosphere shaders read.
struct PackedAtmosphereLight {
  //! Unit direction toward the light; w is 1 when the light is enabled.
  std::array<float, 4> direction_enabled {};
  //! Illuminance in lux, zero for a disabled light.
  std::array<float, 4> illuminance_rgb {};
};

//! Pack `light` for the atmosphere shaders. A disabled light, or one without a
//! usable direction, points along +Z.
[[nodiscard]] inline auto PackAtmosphereLight(const AtmosphereLightModel& light)
  -> PackedAtmosphereLight
{
  constexpr float kMinDirectionLengthSq = 1.0e-6F;
  auto direction = glm::vec3 { 0.0F, 0.0F, 1.0F };
  if (light.enabled
    && glm::dot(light.direction_to_light_ws, light.direction_to_light_ws)
      > kMinDirectionLengthSq) {
    direction = glm::normalize(light.direction_to_light_ws);
  }
  // UE passes GetOuterSpaceIlluminance() here. The Vortex light model already
  // stores intensity-scaled RGB illuminance in lux, the equivalent quantity.
  return {
    .direction_enabled = PackFloat4(direction, light.enabled ? 1.0F : 0.0F),
    .illuminance_rgb = PackFloat4(
      light.enabled ? light.illuminance_rgb_lux : glm::vec3 { 0.0F }),
  };
}

//! One ozone density layer as the `float4` the atmosphere shaders read: width
//! in kilometers, exponential term, linear term per kilometer and constant.
[[nodiscard]] inline auto PackOzoneDensityLayer(
  const engine::atmos::DensityLayer& layer) -> std::array<float, 4>
{
  return {
    engine::atmos::MetersToSkyUnit(layer.width_m),
    layer.exp_term,
    layer.linear_term * engine::atmos::kSkyUnitToM,
    layer.constant_term,
  };
}

} // namespace oxygen::vortex::environment::internal
