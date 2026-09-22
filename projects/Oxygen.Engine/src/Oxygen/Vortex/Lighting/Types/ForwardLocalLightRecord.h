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

struct alignas(packing::kShaderDataFieldAlignment) ForwardLocalLightRecord {
  glm::vec3 position_ws { 0.0F };
  float range_m { 0.0F };
  glm::vec3 intensity_rgb_cd { 0.0F };
  float source_radius_m { 0.0F };
  glm::vec3 emitted_direction_ws { 0.0F, -1.0F, 0.0F };
  float inverse_range_m { 0.0F };
  float inner_cone_sin_half_squared { 0.0F };
  float outer_cone_sin_half_squared { 0.0F };
  std::uint32_t kind { 0U };
  std::uint32_t flags { 0U };
  LightSelectionIndex selection_index { kInvalidLightSelectionIndex };
  std::array<std::uint32_t, 3> reserved {};
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(sizeof(ForwardLocalLightRecord) == 80U);
static_assert(alignof(ForwardLocalLightRecord) == 16U);
static_assert(std::is_standard_layout_v<ForwardLocalLightRecord>);
static_assert(std::is_trivially_copyable_v<ForwardLocalLightRecord>);
static_assert(offsetof(ForwardLocalLightRecord, position_ws) == 0U);
static_assert(offsetof(ForwardLocalLightRecord, range_m) == 12U);
static_assert(offsetof(ForwardLocalLightRecord, intensity_rgb_cd) == 16U);
static_assert(offsetof(ForwardLocalLightRecord, source_radius_m) == 28U);
static_assert(offsetof(ForwardLocalLightRecord, emitted_direction_ws) == 32U);
static_assert(offsetof(ForwardLocalLightRecord, inverse_range_m) == 44U);
static_assert(
  offsetof(ForwardLocalLightRecord, inner_cone_sin_half_squared) == 48U);
static_assert(
  offsetof(ForwardLocalLightRecord, outer_cone_sin_half_squared) == 52U);
static_assert(offsetof(ForwardLocalLightRecord, kind) == 56U);
static_assert(offsetof(ForwardLocalLightRecord, flags) == 60U);
static_assert(offsetof(ForwardLocalLightRecord, selection_index) == 64U);
static_assert(offsetof(ForwardLocalLightRecord, reserved) == 68U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
