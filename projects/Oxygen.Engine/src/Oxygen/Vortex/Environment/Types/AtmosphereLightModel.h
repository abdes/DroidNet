//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace oxygen::vortex::environment {

inline constexpr std::uint32_t kAtmosphereLightSlotCount = 2U;
inline constexpr std::uint32_t kInvalidAtmosphereLightSlot = 0xFFFFFFFFU;

struct AtmosphereLightModel {
  bool enabled { false };
  bool use_per_pixel_transmittance { false };
  std::uint32_t slot_index { 0U };
  std::uint32_t reserved0 { 0U };
  // World-space vector from the shaded point toward the light source.
  // This matches the renderer/light-selection convention and is the UE-style
  // "towards the light" direction after mapping into Oxygen's Z-up, -Y-forward
  // world basis.
  glm::vec3 direction_to_light_ws { 0.0F, 0.0F, 1.0F };
  float angular_size_radians { 0.0F };
  glm::vec3 illuminance_rgb_lux { 0.0F, 0.0F, 0.0F };
  float illuminance_lux { 0.0F };
  glm::vec4 disk_luminance_scale_rgba { 1.0F, 1.0F, 1.0F, 1.0F };
};

using AtmosphereLightSlots
  = std::array<AtmosphereLightModel, kAtmosphereLightSlotCount>;

} // namespace oxygen::vortex::environment
