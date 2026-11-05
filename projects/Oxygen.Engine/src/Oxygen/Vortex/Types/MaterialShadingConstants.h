//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

//! How a material samples one of its textures.
/*!
 @note Layout mirrors HLSL `MaterialSlotSampling` in
 `Vortex/Contracts/Draw/MaterialShadingConstants.hlsli`.
*/
struct MaterialSlotSampling {
  //! A UV's coordinates multiply these two, and the offset then adds.
  glm::vec2 uv_basis_u { 1.0F, 0.0F };
  glm::vec2 uv_basis_v { 0.0F, 1.0F };
  glm::vec2 uv_offset { 0.0F, 0.0F };
  //! The vertex UV set read, 0 or 1.
  uint32_t uv_set { 0U };
  //! Where `SamplerDescriptorHeap` holds this slot's sampler.
  ShaderVisibleIndex sampler_index { TextureSamplerIndex({}) };

  auto operator==(const MaterialSlotSampling&) const -> bool = default;
};
static_assert(sizeof(MaterialSlotSampling) == 32);

//! A slot's record for an authored transform: scale, then counter-clockwise
//! rotation around the UV origin, then offset.
[[nodiscard]] inline auto MakeMaterialSlotSampling(const uint32_t uv_set,
  const glm::vec2 uv_scale, const glm::vec2 uv_offset,
  const float uv_rotation_radians,
  const TextureSamplerState& sampler = {}) noexcept -> MaterialSlotSampling
{
  const auto c = std::cos(uv_rotation_radians);
  const auto s = std::sin(uv_rotation_radians);
  return MaterialSlotSampling {
    .uv_basis_u = { uv_scale.x * c, uv_scale.x * s },
    .uv_basis_v = { -uv_scale.y * s, uv_scale.y * c },
    .uv_offset = uv_offset,
    .uv_set = uv_set,
    .sampler_index = TextureSamplerIndex(sampler),
  };
}

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
  float alpha_cutoff { 0.5F };
  uint32_t _pad2 { 0U };

  // Registers 5 to 16, one record per sampled texture slot.
  MaterialSlotSampling base_color_sampling {};
  MaterialSlotSampling normal_sampling {};
  MaterialSlotSampling metallic_sampling {};
  MaterialSlotSampling roughness_sampling {};
  MaterialSlotSampling ambient_occlusion_sampling {};
  MaterialSlotSampling emissive_sampling {};
};
static_assert(sizeof(ShaderVisibleIndex) == sizeof(uint32_t));
static_assert(offsetof(MaterialShadingConstants, base_color_sampling) == 80U);
static_assert(
  offsetof(MaterialShadingConstants, ambient_occlusion_texture_index) == 64U);
static_assert(
  offsetof(MaterialShadingConstants, emissive_texture_index) == 68U);
static_assert(offsetof(MaterialShadingConstants, alpha_cutoff) == 72U);
static_assert(offsetof(MaterialShadingConstants, _pad2) == 76U);
static_assert(
  sizeof(MaterialShadingConstants) % packing::kShaderDataFieldAlignment == 0);
static_assert(sizeof(MaterialShadingConstants) == 272);

} // namespace oxygen::vortex
