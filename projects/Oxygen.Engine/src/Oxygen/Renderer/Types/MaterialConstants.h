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

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Per-material (draw-scope) constants snapshot.
/*! \brief Layout mirrors HLSL cbuffer MaterialConstants (b2, space0).

Fields (match shader order):
  - base_color (float4)
  - metalness (float)
  - roughness (float)
  - normal_scale (float)
  - ambient_occlusion (float)
  - base_color_texture_index (uint)
  - normal_texture_index (uint)
  - metallic_texture_index (uint)
  - roughness_texture_index (uint)
  - ambient_occlusion_texture_index (uint)
  - opacity_texture_index (uint)
  - flags (uint)
  - alpha_cutoff (float)
  - uv_scale (float2)
  - uv_offset (float2)
  - uv_rotation_radians (float)
  - uv_set (uint)

The final two floats pad to a 16-byte multiple so the struct size is root-CBV
friendly. Provided as a whole-snapshot API similar to SceneConstants.
*/
struct MaterialConstants {
  glm::vec4 base_color { 1.0F, 1.0F, 1.0F, 1.0F };
  float metalness { 0.0F };
  float roughness { 1.0F };
  float normal_scale { 1.0F };
  float ambient_occlusion { 1.0F };
  uint32_t base_color_texture_index { 0 };
  uint32_t normal_texture_index { 0 };
  uint32_t metallic_texture_index { 0 };
  uint32_t roughness_texture_index { 0 };
  uint32_t ambient_occlusion_texture_index { 0 };
  uint32_t opacity_texture_index { 0 };
  uint32_t flags { 0 };
  float alpha_cutoff { 0.5F };
  glm::vec2 uv_scale { 1.0F, 1.0F };
  glm::vec2 uv_offset { 0.0F, 0.0F };
  float uv_rotation_radians { 0.0F };
  uint32_t uv_set { 0U };
  glm::vec3 emissive_factor { 0.0F, 0.0F, 0.0F };
  uint32_t emissive_texture_index { 0xFFFFFFFFU };
  glm::vec2 padding { 0.0F, 0.0F };
};
static_assert(sizeof(MaterialConstants) == 112,
  "MaterialConstants size must be 112 bytes (7 x 16-byte rows)");
static_assert(sizeof(MaterialConstants) % 16 == 0,
  "MaterialConstants size must be 16-byte aligned");

} // namespace oxygen::engine
