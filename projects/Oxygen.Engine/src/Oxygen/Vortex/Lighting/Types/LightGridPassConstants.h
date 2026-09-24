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
#include <glm/vec4.hpp>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex {

//! Immutable structured dispatch payload for per-view count/scan/scatter work.
struct alignas(16) LightGridPassConstants {
  ShaderVisibleIndex lighting_bindings_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex ranges_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex indices_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex status_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex counts_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex offsets_uav { kInvalidShaderVisibleIndex };
  std::uint32_t subpass { 0U };
  std::uint32_t work_count { 0U };
  std::array<std::uint32_t, 2> work_offset {};
  std::uint32_t scan_stride { 0U };
  std::uint32_t scan_phase { 0U };
  ShaderVisibleIndex scan_destination_uav { kInvalidShaderVisibleIndex };
  std::array<std::uint32_t, 3> reserved {};
  glm::mat4 view_matrix { 1.0F };
  glm::mat4 inverse_projection { 1.0F };
  glm::vec4 depth_projection { 0.0F };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<LightGridPassConstants>);
static_assert(std::is_trivially_copyable_v<LightGridPassConstants>);
static_assert(alignof(LightGridPassConstants) == 16U);
static_assert(sizeof(LightGridPassConstants) == 208U);
static_assert(offsetof(LightGridPassConstants, scan_destination_uav) == 48U);
static_assert(offsetof(LightGridPassConstants, view_matrix) == 64U);
static_assert(offsetof(LightGridPassConstants, inverse_projection) == 128U);
static_assert(offsetof(LightGridPassConstants, depth_projection) == 192U);
static_assert(offsetof(LightGridPassConstants, lighting_bindings_srv) == 0U);
static_assert(offsetof(LightGridPassConstants, ranges_uav) == 4U);
static_assert(offsetof(LightGridPassConstants, indices_uav) == 8U);
static_assert(offsetof(LightGridPassConstants, status_uav) == 12U);
static_assert(offsetof(LightGridPassConstants, counts_uav) == 16U);
static_assert(offsetof(LightGridPassConstants, offsets_uav) == 20U);
static_assert(offsetof(LightGridPassConstants, subpass) == 24U);
static_assert(offsetof(LightGridPassConstants, work_count) == 28U);
static_assert(offsetof(LightGridPassConstants, work_offset) == 32U);
static_assert(offsetof(LightGridPassConstants, scan_stride) == 40U);
static_assert(offsetof(LightGridPassConstants, scan_phase) == 44U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
