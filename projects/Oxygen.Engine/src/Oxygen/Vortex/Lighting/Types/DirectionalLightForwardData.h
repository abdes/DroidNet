//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) DirectionalLightForwardData {
  glm::vec3 direction_to_source_ws { 0.0F, -1.0F, 0.0F };
  AtmosphereLightIndex atmosphere_light_slot { kInvalidAtmosphereLightIndex };
  glm::vec3 illuminance_rgb_lux { 0.0F };
  std::uint32_t flags { 0U };
  glm::vec3 ground_transmittance_rgb { 1.0F };
  std::uint32_t atmosphere_mode_flags { 0U };
  LightSelectionIndex selection_index { kInvalidLightSelectionIndex };
  std::array<std::uint32_t, 3> reserved {};
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(sizeof(DirectionalLightForwardData) == 64U);
static_assert(alignof(DirectionalLightForwardData) == 16U);
static_assert(std::is_standard_layout_v<DirectionalLightForwardData>);
static_assert(std::is_trivially_copyable_v<DirectionalLightForwardData>);
static_assert(
  offsetof(DirectionalLightForwardData, direction_to_source_ws) == 0U);
static_assert(
  offsetof(DirectionalLightForwardData, atmosphere_light_slot) == 12U);
static_assert(
  offsetof(DirectionalLightForwardData, illuminance_rgb_lux) == 16U);
static_assert(offsetof(DirectionalLightForwardData, flags) == 28U);
static_assert(
  offsetof(DirectionalLightForwardData, ground_transmittance_rgb) == 32U);
static_assert(
  offsetof(DirectionalLightForwardData, atmosphere_mode_flags) == 44U);
static_assert(offsetof(DirectionalLightForwardData, selection_index) == 48U);
static_assert(offsetof(DirectionalLightForwardData, reserved) == 52U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
