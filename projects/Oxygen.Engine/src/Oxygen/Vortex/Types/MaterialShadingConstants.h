//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

//! Per-material shading constants snapshot.
/*!
 Core material ABI for general shading data. Specialized material
 * extensions
 such as procedural-grid parameters live in separate tables keyed
 * by the same
 material handle.

 @note Layout mirrors HLSL
 * `MaterialShadingConstants` in
 `Vortex/Contracts/Draw/MaterialShadingConstants.hlsli`.

 * @note Opacity is implicitly sampled from the alpha channel of the base color

 * texture (glTF convention).
*/
struct alignas(packing::kShaderDataFieldAlignment) MaterialShadingConstants {
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
  ShaderVisibleIndex emissive_texture_index { kInvalidShaderVisibleIndex };
  float alpha_cutoff { 0.5F }; // NOLINT(*-magic-numbers)
  uint32_t _pad2 { 0U };

  // Register 5
  glm::vec2 uv_scale { 1.0F, 1.0F };
  glm::vec2 uv_offset { 0.0F, 0.0F };

  // Register 6
  float uv_rotation_radians { 0.0F };
  uint32_t uv_set { 0U };
  uint32_t _pad0 { 0U };
  uint32_t _pad1 { 0U };
};
static_assert(sizeof(ShaderVisibleIndex) == sizeof(uint32_t));
static_assert(
  offsetof(MaterialShadingConstants, ambient_occlusion_texture_index) == 64U);
static_assert(
  offsetof(MaterialShadingConstants, emissive_texture_index) == 68U);
static_assert(offsetof(MaterialShadingConstants, alpha_cutoff) == 72U);
static_assert(offsetof(MaterialShadingConstants, _pad2) == 76U);
static_assert(
  sizeof(MaterialShadingConstants) % packing::kShaderDataFieldAlignment == 0);
static_assert(
  sizeof(MaterialShadingConstants) <= packing::kRootConstantsMaxSize);
static_assert(
  sizeof(MaterialShadingConstants) == 112); // NOLINT(*-magic-numbers)

} // namespace oxygen::vortex
