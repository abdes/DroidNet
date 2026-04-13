//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

//! Optional per-material extension payload for procedural-grid shading.
/*!
 Indexed by the same `MaterialHandle` row as `MaterialShadingConstants`, but
 published through its own structured SRV so the core material ABI remains
 focused on general shading data.
*/
struct alignas(packing::kShaderDataFieldAlignment)
  ProceduralGridMaterialConstants {
  glm::vec2 grid_spacing { 0.0F, 0.0F };
  uint32_t grid_major_every { 0U };
  float grid_line_thickness { 0.0F };

  float grid_major_thickness { 0.0F };
  float grid_axis_thickness { 0.0F };
  float grid_fade_start { 0.0F };
  float grid_fade_end { 0.0F };

  glm::vec4 grid_minor_color { 0.0F, 0.0F, 0.0F, 0.0F };
  glm::vec4 grid_major_color { 0.0F, 0.0F, 0.0F, 0.0F };
  glm::vec4 grid_axis_color_x { 0.0F, 0.0F, 0.0F, 0.0F };
  glm::vec4 grid_axis_color_y { 0.0F, 0.0F, 0.0F, 0.0F };
  glm::vec4 grid_origin_color { 0.0F, 0.0F, 0.0F, 0.0F };
};

static_assert(
  sizeof(ProceduralGridMaterialConstants) % packing::kShaderDataFieldAlignment
  == 0);
static_assert(
  sizeof(ProceduralGridMaterialConstants) == 112); // NOLINT(*-magic-numbers)

} // namespace oxygen::vortex
