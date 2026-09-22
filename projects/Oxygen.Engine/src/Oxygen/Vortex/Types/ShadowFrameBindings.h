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

#include <glm/vec2.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kShadowSamplingReversedZPcf = 1U;

//! Per-view routing header for separately published canonical shadow records.
struct alignas(packing::kShaderDataFieldAlignment) ShadowFrameBindings {
  ShaderVisibleIndex directional_records_srv { kInvalidShaderVisibleIndex };
  std::uint32_t directional_record_count { 0U };
  ShaderVisibleIndex projected_local_records_srv { kInvalidShaderVisibleIndex };
  std::uint32_t projected_local_record_count { 0U };
  ShaderVisibleIndex cube_local_records_srv { kInvalidShaderVisibleIndex };
  std::uint32_t cube_local_record_count { 0U };
  ShaderVisibleIndex cascade_records_srv { kInvalidShaderVisibleIndex };
  std::uint32_t cascade_record_count { 0U };
  ShaderVisibleIndex contact_depth_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex view_status_srv { kInvalidShaderVisibleIndex };
  std::uint32_t contact_enabled { 0U };
  std::uint32_t sampling_flags { 0U };
  glm::vec2 contact_content_origin_px { 0.0F };
  glm::vec2 contact_content_extent_px { 0.0F };
  std::array<std::uint32_t, 2> scene_generation { 0U };
  std::array<std::uint32_t, 2> selection_revision { 0U };
  std::array<std::uint32_t, 2> frame_sequence { 0U };
  std::array<std::uint32_t, 2> view_generation { 0U };
  glm::uvec2 contact_texture_extent_px { 0U };
  glm::uvec2 reserved { 0U };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<ShadowFrameBindings>);
static_assert(std::is_trivially_copyable_v<ShadowFrameBindings>);
static_assert(sizeof(ShadowFrameBindings) == 112U);
static_assert(alignof(ShadowFrameBindings) == 16U);
static_assert(offsetof(ShadowFrameBindings, directional_records_srv) == 0U);
static_assert(offsetof(ShadowFrameBindings, directional_record_count) == 4U);
static_assert(offsetof(ShadowFrameBindings, projected_local_records_srv) == 8U);
static_assert(
  offsetof(ShadowFrameBindings, projected_local_record_count) == 12U);
static_assert(offsetof(ShadowFrameBindings, cube_local_records_srv) == 16U);
static_assert(offsetof(ShadowFrameBindings, cube_local_record_count) == 20U);
static_assert(offsetof(ShadowFrameBindings, cascade_records_srv) == 24U);
static_assert(offsetof(ShadowFrameBindings, cascade_record_count) == 28U);
static_assert(offsetof(ShadowFrameBindings, contact_depth_srv) == 32U);
static_assert(offsetof(ShadowFrameBindings, view_status_srv) == 36U);
static_assert(offsetof(ShadowFrameBindings, contact_enabled) == 40U);
static_assert(offsetof(ShadowFrameBindings, sampling_flags) == 44U);
static_assert(offsetof(ShadowFrameBindings, contact_content_origin_px) == 48U);
static_assert(offsetof(ShadowFrameBindings, contact_content_extent_px) == 56U);
static_assert(offsetof(ShadowFrameBindings, scene_generation) == 64U);
static_assert(offsetof(ShadowFrameBindings, selection_revision) == 72U);
static_assert(offsetof(ShadowFrameBindings, frame_sequence) == 80U);
static_assert(offsetof(ShadowFrameBindings, view_generation) == 88U);
static_assert(offsetof(ShadowFrameBindings, contact_texture_extent_px) == 96U);
static_assert(offsetof(ShadowFrameBindings, reserved) == 104U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
