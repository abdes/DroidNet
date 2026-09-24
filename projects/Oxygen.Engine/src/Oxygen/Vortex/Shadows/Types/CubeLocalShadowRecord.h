//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
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
struct alignas(packing::kShaderDataFieldAlignment) CubeLocalShadowRecord {
  static constexpr std::uint32_t kFaceCount = 6U;
  std::array<glm::mat4, kFaceCount> face_light_view_projection {};
  glm::vec3 shadow_origin_ws { 0.0F };
  float near_plane_m { 0.0F };
  float far_plane_m { 0.0F };
  float normal_bias_m { 0.0F };
  float depth_bias { 0.0F }; // Clip-space receiver bias; divide by clip W.
  float world_texel_size {
    0.0F
  }; // Far-plane footprint; scale by receiver depth.
  ShaderVisibleIndex surface_srv { kInvalidShaderVisibleIndex }; // Cube array.
  ShadowArrayLayer first_array_layer { kInvalidShadowArrayLayer };
  LightSelectionIndex selection_index { kInvalidLightSelectionIndex };
  float shadow_strength { 1.0F };
  glm::vec2 inverse_resolution { 0.0F };
  std::uint32_t pcf_sample_count { 29U };
  std::uint32_t reserved1 { 0U };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<CubeLocalShadowRecord>);
static_assert(std::is_trivially_copyable_v<CubeLocalShadowRecord>);
static_assert(alignof(CubeLocalShadowRecord) == 16U);
static_assert(sizeof(CubeLocalShadowRecord) == 448U);
static_assert(
  offsetof(CubeLocalShadowRecord, face_light_view_projection) == 0U);
static_assert(offsetof(CubeLocalShadowRecord, shadow_origin_ws) == 384U);
static_assert(offsetof(CubeLocalShadowRecord, near_plane_m) == 396U);
static_assert(offsetof(CubeLocalShadowRecord, far_plane_m) == 400U);
static_assert(offsetof(CubeLocalShadowRecord, normal_bias_m) == 404U);
static_assert(offsetof(CubeLocalShadowRecord, depth_bias) == 408U);
static_assert(offsetof(CubeLocalShadowRecord, world_texel_size) == 412U);
static_assert(offsetof(CubeLocalShadowRecord, surface_srv) == 416U);
static_assert(offsetof(CubeLocalShadowRecord, first_array_layer) == 420U);
static_assert(offsetof(CubeLocalShadowRecord, selection_index) == 424U);
static_assert(offsetof(CubeLocalShadowRecord, shadow_strength) == 428U);
static_assert(offsetof(CubeLocalShadowRecord, inverse_resolution) == 432U);
static_assert(offsetof(CubeLocalShadowRecord, pcf_sample_count) == 440U);
static_assert(offsetof(CubeLocalShadowRecord, reserved1) == 444U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
