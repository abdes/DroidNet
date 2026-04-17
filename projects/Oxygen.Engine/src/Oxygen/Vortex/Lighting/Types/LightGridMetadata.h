//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) LightGridMetadata {
  glm::ivec3 grid_size { 0 };
  float reserved0 { 0.0F };

  glm::vec3 grid_z_params { 0.0F };
  float reserved1 { 0.0F };

  std::uint32_t num_grid_cells { 0U };
  std::uint32_t max_culled_lights_per_cell { 0U };
  std::uint32_t local_light_count { 0U };
  std::uint32_t directional_light_count { 0U };

  glm::vec4 pre_view_translation_offset { 0.0F };
};

static_assert(alignof(LightGridMetadata) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(LightGridMetadata) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::vortex
