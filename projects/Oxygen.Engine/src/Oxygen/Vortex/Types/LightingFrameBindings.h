//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>

namespace oxygen::vortex {

//! Bindless lighting-system routing payload for a single view.
struct alignas(packing::kShaderDataFieldAlignment) LightingFrameBindings {
  ShaderVisibleIndex local_light_buffer_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex light_view_data_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex grid_metadata_buffer_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex grid_indirection_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex directional_light_indices_srv {
    kInvalidShaderVisibleIndex
  };

  glm::ivec3 grid_size { 0 };
  float reserved_grid0 { 0.0F };

  glm::vec3 grid_z_params { 0.0F };
  float reserved_grid1 { 0.0F };

  std::uint32_t num_grid_cells { 0U };
  std::uint32_t max_culled_lights_per_cell { 0U };
  std::uint32_t directional_light_count { 0U };
  std::uint32_t local_light_count { 0U };

  std::uint32_t has_directional_light { 0U };
  std::uint32_t affects_translucent_lighting { 0U };
  std::uint32_t flags { 0U };
  std::uint32_t reserved_flags { 0U };

  glm::vec4 pre_view_translation_offset { 0.0F };
  std::array<std::uint32_t, 3> reserved_directional_alignment {};

  DirectionalLightForwardData directional {};

  // Compatibility slots retained while later Phase 4 consumers migrate from
  // the old placeholder binding shape to the richer lighting contract.
  ShaderVisibleIndex directional_lights_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex positional_lights_slot { kInvalidShaderVisibleIndex };
  std::array<std::uint32_t, 2> reserved_tail {};
};

static_assert(
  alignof(LightingFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(sizeof(LightingFrameBindings) == 208);
static_assert(offsetof(LightingFrameBindings, has_directional_light) == 68);
static_assert(
  offsetof(LightingFrameBindings, pre_view_translation_offset) == 84);
static_assert(
  offsetof(LightingFrameBindings, reserved_directional_alignment) == 100);
static_assert(offsetof(LightingFrameBindings, directional) == 112);
static_assert(offsetof(LightingFrameBindings, directional_lights_slot) == 192);
static_assert(offsetof(LightingFrameBindings, positional_lights_slot) == 196);
static_assert(offsetof(LightingFrameBindings, reserved_tail) == 200);

} // namespace oxygen::vortex
