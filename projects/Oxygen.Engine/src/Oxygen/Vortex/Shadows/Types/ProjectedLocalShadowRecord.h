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
#include <glm/vec3.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex {

//! Derived local shadow projection, independent of the selected light kind.
struct alignas(packing::kShaderDataFieldAlignment) ProjectedLocalShadowRecord {
  glm::mat4 light_view_projection { 1.0F };
  glm::vec3 shadow_origin_ws { 0.0F };
  float near_plane_m { 0.0F };
  float far_plane_m { 0.0F };
  float normal_bias_m { 0.0F };
  float depth_bias { 0.0F };
  float world_texel_size { 0.0F };
  ShaderVisibleIndex surface_srv { kInvalidShaderVisibleIndex };
  ShadowArrayLayer array_layer { kInvalidShadowArrayLayer };
  LightSelectionIndex selection_index { kInvalidLightSelectionIndex };
  float shadow_strength { 1.0F };
  glm::vec2 inverse_resolution { 0.0F };
  glm::uvec2 reserved1 { 0U };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<ProjectedLocalShadowRecord>);
static_assert(std::is_trivially_copyable_v<ProjectedLocalShadowRecord>);
static_assert(alignof(ProjectedLocalShadowRecord) == 16U);
static_assert(sizeof(ProjectedLocalShadowRecord) == 128U);
static_assert(
  offsetof(ProjectedLocalShadowRecord, light_view_projection) == 0U);
static_assert(offsetof(ProjectedLocalShadowRecord, shadow_origin_ws) == 64U);
static_assert(offsetof(ProjectedLocalShadowRecord, near_plane_m) == 76U);
static_assert(offsetof(ProjectedLocalShadowRecord, far_plane_m) == 80U);
static_assert(offsetof(ProjectedLocalShadowRecord, normal_bias_m) == 84U);
static_assert(offsetof(ProjectedLocalShadowRecord, depth_bias) == 88U);
static_assert(offsetof(ProjectedLocalShadowRecord, world_texel_size) == 92U);
static_assert(offsetof(ProjectedLocalShadowRecord, surface_srv) == 96U);
static_assert(offsetof(ProjectedLocalShadowRecord, array_layer) == 100U);
static_assert(offsetof(ProjectedLocalShadowRecord, selection_index) == 104U);
static_assert(offsetof(ProjectedLocalShadowRecord, shadow_strength) == 108U);
static_assert(offsetof(ProjectedLocalShadowRecord, inverse_resolution) == 112U);
static_assert(offsetof(ProjectedLocalShadowRecord, reserved1) == 120U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
