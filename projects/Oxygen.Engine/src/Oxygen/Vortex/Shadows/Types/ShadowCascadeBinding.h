//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex {

//! One directional projection, with explicit surface identity and sampling
//! units.
struct alignas(packing::kShaderDataFieldAlignment) ShadowCascadeBinding {
  glm::mat4 light_view_projection { 1.0F };
  float split_near { 0.0F };
  float split_far { 0.0F };
  float depth_bias { 0.0F };
  float normal_bias_m { 0.0F };
  ShaderVisibleIndex surface_srv { kInvalidShaderVisibleIndex };
  ShadowArrayLayer array_layer { kInvalidShadowArrayLayer };
  glm::uvec2 reserved0 { 0U };
  glm::vec2 inverse_resolution { 0.0F };
  float world_texel_size { 0.0F };
  float transition_width { 0.0F };
  float fade_begin { 0.0F };
  float fade_end { 0.0F };
  glm::uvec2 reserved1 { 0U };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<ShadowCascadeBinding>);
static_assert(std::is_trivially_copyable_v<ShadowCascadeBinding>);
static_assert(alignof(ShadowCascadeBinding) == 16U);
static_assert(sizeof(ShadowCascadeBinding) == 128U);
static_assert(offsetof(ShadowCascadeBinding, light_view_projection) == 0U);
static_assert(offsetof(ShadowCascadeBinding, split_near) == 64U);
static_assert(offsetof(ShadowCascadeBinding, split_far) == 68U);
static_assert(offsetof(ShadowCascadeBinding, depth_bias) == 72U);
static_assert(offsetof(ShadowCascadeBinding, normal_bias_m) == 76U);
static_assert(offsetof(ShadowCascadeBinding, surface_srv) == 80U);
static_assert(offsetof(ShadowCascadeBinding, array_layer) == 84U);
static_assert(offsetof(ShadowCascadeBinding, reserved0) == 88U);
static_assert(offsetof(ShadowCascadeBinding, inverse_resolution) == 96U);
static_assert(offsetof(ShadowCascadeBinding, world_texel_size) == 104U);
static_assert(offsetof(ShadowCascadeBinding, transition_width) == 108U);
static_assert(offsetof(ShadowCascadeBinding, fade_begin) == 112U);
static_assert(offsetof(ShadowCascadeBinding, fade_end) == 116U);
static_assert(offsetof(ShadowCascadeBinding, reserved1) == 120U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
