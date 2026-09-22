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

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kLightingPublicationDisabled = 0U;
inline constexpr std::uint32_t kLightingPublicationEmpty = 1U;
inline constexpr std::uint32_t kLightingPublicationRecorded = 2U;
inline constexpr std::uint32_t kLightingPublicationFailed = 3U;

struct alignas(16) LightingFrameBindings {
  ShaderVisibleIndex directional_records_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex local_records_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex cluster_ranges_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex local_indices_srv { kInvalidShaderVisibleIndex };
  std::uint32_t directional_count { 0U };
  std::uint32_t local_count { 0U };
  std::uint32_t cluster_count { 0U };
  std::uint32_t index_capacity { 0U };
  ShaderVisibleIndex directional_shadow_map_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex local_shadow_map_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex build_status_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex grid_metadata_srv { kInvalidShaderVisibleIndex };
  std::array<std::uint32_t, 2> scene_generation {};
  std::array<std::uint32_t, 2> selection_revision {};
  std::array<std::uint32_t, 2> frame_sequence {};
  std::array<std::uint32_t, 2> view_generation {};
  std::uint32_t publication_state { 0U };
  ShaderVisibleIndex brdf_moments_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex brdf_mean_moments_srv { kInvalidShaderVisibleIndex };
  std::uint32_t brdf_model_revision { 0U };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(sizeof(LightingFrameBindings) == 96U);
static_assert(alignof(LightingFrameBindings) == 16U);
static_assert(std::is_standard_layout_v<LightingFrameBindings>);
static_assert(std::is_trivially_copyable_v<LightingFrameBindings>);
static_assert(offsetof(LightingFrameBindings, directional_records_srv) == 0U);
static_assert(offsetof(LightingFrameBindings, local_records_srv) == 4U);
static_assert(offsetof(LightingFrameBindings, cluster_ranges_srv) == 8U);
static_assert(offsetof(LightingFrameBindings, local_indices_srv) == 12U);
static_assert(offsetof(LightingFrameBindings, directional_count) == 16U);
static_assert(offsetof(LightingFrameBindings, local_count) == 20U);
static_assert(offsetof(LightingFrameBindings, cluster_count) == 24U);
static_assert(offsetof(LightingFrameBindings, index_capacity) == 28U);
static_assert(
  offsetof(LightingFrameBindings, directional_shadow_map_srv) == 32U);
static_assert(offsetof(LightingFrameBindings, local_shadow_map_srv) == 36U);
static_assert(offsetof(LightingFrameBindings, build_status_srv) == 40U);
static_assert(offsetof(LightingFrameBindings, grid_metadata_srv) == 44U);
static_assert(offsetof(LightingFrameBindings, scene_generation) == 48U);
static_assert(offsetof(LightingFrameBindings, selection_revision) == 56U);
static_assert(offsetof(LightingFrameBindings, frame_sequence) == 64U);
static_assert(offsetof(LightingFrameBindings, view_generation) == 72U);
static_assert(offsetof(LightingFrameBindings, publication_state) == 80U);
static_assert(offsetof(LightingFrameBindings, brdf_moments_srv) == 84U);
static_assert(offsetof(LightingFrameBindings, brdf_mean_moments_srv) == 88U);
static_assert(offsetof(LightingFrameBindings, brdf_model_revision) == 92U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
