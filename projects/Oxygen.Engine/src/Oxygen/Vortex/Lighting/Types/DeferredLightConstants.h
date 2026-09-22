//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/ext/matrix_float4x4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex {

struct alignas(16) DeferredLightConstants {
  glm::mat4 light_world_matrix { 1.0F };
  std::uint32_t light_type { 0U };
  LightSelectionIndex selection_index { kInvalidLightSelectionIndex };
  ShaderVisibleIndex light_geometry_vertices_srv { kInvalidShaderVisibleIndex };
  std::uint32_t light_geometry_vertex_count { 0U };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(sizeof(DeferredLightConstants) == 80U);
static_assert(alignof(DeferredLightConstants) == 16U);
static_assert(std::is_standard_layout_v<DeferredLightConstants>);
static_assert(std::is_trivially_copyable_v<DeferredLightConstants>);
static_assert(offsetof(DeferredLightConstants, light_world_matrix) == 0U);
static_assert(offsetof(DeferredLightConstants, light_type) == 64U);
static_assert(offsetof(DeferredLightConstants, selection_index) == 68U);
static_assert(
  offsetof(DeferredLightConstants, light_geometry_vertices_srv) == 72U);
static_assert(
  offsetof(DeferredLightConstants, light_geometry_vertex_count) == 76U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
