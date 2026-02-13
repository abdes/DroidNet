//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Per-material (draw-scope) constants snapshot.
/*!
 @note Layout mirrors HLSL cbuffer MaterialConstants (b2, space0).
*/
struct alignas(packing::kShaderDataFieldAlignment) MaterialConstants {
  // Register 0
  glm::vec4 base_color { 1.0F, 1.0F, 1.0F, 1.0F };

  // Register 1
  glm::vec3 emissive_factor { 0.0F, 0.0F, 0.0F };
  uint32_t flags { 0 };

  // Register 2
  float metalness { 0.0F };
  float roughness { 1.0F };
  float normal_scale { 1.0F };
  float ambient_occlusion { 1.0F };

  // Register 3
  ShaderVisibleIndex base_color_texture_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex normal_texture_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex metallic_texture_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex roughness_texture_index { kInvalidShaderVisibleIndex };

  // Register 4
  ShaderVisibleIndex ambient_occlusion_texture_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex opacity_texture_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex emissive_texture_index { kInvalidShaderVisibleIndex };
  float alpha_cutoff { 0.5F }; // NOLINT(*-magic-numbers)

  // Register 5
  glm::vec2 uv_scale { 1.0F, 1.0F };
  glm::vec2 uv_offset { 0.0F, 0.0F };

  // Register 6
  float uv_rotation_radians { 0.0F };
  uint32_t uv_set { 0U };
  uint32_t _pad0 { 0U };
  uint32_t _pad1 { 0U };

  // Register 7
  glm::vec2 grid_spacing { 0.0F, 0.0F };
  uint32_t grid_major_every { 0U };
  float grid_line_thickness { 0.0F };

  // Register 8
  float grid_major_thickness { 0.0F };
  float grid_axis_thickness { 0.0F };
  float grid_fade_start { 0.0F };
  float grid_fade_end { 0.0F };

  // Register 9
  glm::vec4 grid_minor_color { 0.0F, 0.0F, 0.0F, 0.0F };

  // Register 10
  glm::vec4 grid_major_color { 0.0F, 0.0F, 0.0F, 0.0F };

  // Register 11
  glm::vec4 grid_axis_color_x { 0.0F, 0.0F, 0.0F, 0.0F };

  // Register 12
  glm::vec4 grid_axis_color_y { 0.0F, 0.0F, 0.0F, 0.0F };

  // Register 13
  glm::vec4 grid_origin_color { 0.0F, 0.0F, 0.0F, 0.0F };
};
static_assert(sizeof(ShaderVisibleIndex) == sizeof(uint32_t));
static_assert(
  sizeof(MaterialConstants) % packing::kShaderDataFieldAlignment == 0);
static_assert(sizeof(MaterialConstants) <= packing::kRootConstantsMaxSize);
static_assert(sizeof(MaterialConstants) == 224); // NOLINT(*-magic-numbers)

} // namespace oxygen::engine
