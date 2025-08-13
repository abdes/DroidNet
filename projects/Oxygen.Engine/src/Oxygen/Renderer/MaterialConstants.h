//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
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
  - flags (uint)
  - _pad0 (float)
  - _pad1 (float)

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
  uint32_t flags { 0 };
  uint32_t _pad0 { 0 };
  uint32_t _pad1 { 0 };
};
static_assert(sizeof(MaterialConstants) % 16 == 0,
  "MaterialConstants size must be 16-byte aligned");

} // namespace oxygen::engine
