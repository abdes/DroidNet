//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <glm/mat4x4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::engine::detail {

constexpr std::uint32_t kVirtualShadowMaxSupportedPagesPerAxis = 64U;
constexpr std::uint32_t kVirtualShadowMaxSupportedClipLevels = 12U;

struct alignas(16) ResolvePackedInt4 {
  std::int32_t x { 0 };
  std::int32_t y { 0 };
  std::int32_t z { 0 };
  std::int32_t w { 0 };
};

struct alignas(16) ResolvePackedFloat4 {
  float x { 0.0F };
  float y { 0.0F };
  float z { 0.0F };
  float w { 0.0F };
};

struct alignas(packing::kShaderDataFieldAlignment)
  VirtualShadowPassConstants {
  ShaderVisibleIndex request_words_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_mark_flags_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex draw_bounds_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex previous_shadow_caster_bounds_srv_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex current_shadow_caster_bounds_srv_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex schedule_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex schedule_lookup_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex schedule_count_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex clear_args_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex draw_args_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex draw_page_ranges_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex draw_page_indices_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex draw_page_counter_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_table_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_flags_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex dirty_page_flags_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_metadata_srv_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex physical_page_metadata_uav_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex physical_page_lists_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_lists_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex resolve_stats_uav_index { kInvalidShaderVisibleIndex };
  std::uint32_t _pad0 { 0U };
  std::uint32_t _pad1 { 0U };
  std::uint32_t _pad2 { 0U };
  glm::mat4 current_light_view_matrix { 1.0F };
  glm::mat4 previous_light_view_matrix { 1.0F };
  std::uint32_t shadow_caster_bound_count { 0U };
  std::uint32_t request_word_count { 0U };
  std::uint32_t total_page_count { 0U };
  std::uint32_t schedule_capacity { 0U };
  std::uint32_t pages_per_axis { 0U };
  std::uint32_t clip_level_count { 0U };
  std::uint32_t pages_per_level { 0U };
  std::uint32_t physical_page_capacity { 0U };
  std::uint32_t atlas_tiles_per_axis { 0U };
  std::uint32_t draw_count { 0U };
  std::uint32_t draw_page_list_capacity { 0U };
  std::uint32_t reset_page_management_state { 0U };
  std::uint32_t global_dirty_resident_contents { 0U };
  std::uint32_t phase { 0U };
  std::uint32_t target_clip_index { 0U };
  std::array<ResolvePackedInt4, 3U> clip_grid_origin_x_packed {};
  std::array<ResolvePackedInt4, 3U> clip_grid_origin_y_packed {};
  std::array<ResolvePackedFloat4, 3U> clip_origin_x_packed {};
  std::array<ResolvePackedFloat4, 3U> clip_origin_y_packed {};
  std::array<ResolvePackedFloat4, 3U> clip_page_world_packed {};
};
static_assert(
  sizeof(VirtualShadowPassConstants) % packing::kShaderDataFieldAlignment == 0U);

constexpr std::uint32_t kVirtualShadowPassConstantsStride
  = ((sizeof(VirtualShadowPassConstants)
       + oxygen::packing::kConstantBufferAlignment - 1U)
      / oxygen::packing::kConstantBufferAlignment)
  * oxygen::packing::kConstantBufferAlignment;

inline auto PackInt4(
  const std::array<std::int32_t, kVirtualShadowMaxSupportedClipLevels>& values)
  -> std::array<ResolvePackedInt4, 3U>
{
  std::array<ResolvePackedInt4, 3U> packed {};
  for (std::uint32_t i = 0U; i < kVirtualShadowMaxSupportedClipLevels; ++i) {
    auto& lane = packed[i / 4U];
    switch (i % 4U) {
    case 0U:
      lane.x = values[i];
      break;
    case 1U:
      lane.y = values[i];
      break;
    case 2U:
      lane.z = values[i];
      break;
    default:
      lane.w = values[i];
      break;
    }
  }
  return packed;
}

inline auto PackFloat4(
  const std::array<float, kVirtualShadowMaxSupportedClipLevels>& values)
  -> std::array<ResolvePackedFloat4, 3U>
{
  std::array<ResolvePackedFloat4, 3U> packed {};
  for (std::uint32_t i = 0U; i < kVirtualShadowMaxSupportedClipLevels; ++i) {
    auto& lane = packed[i / 4U];
    switch (i % 4U) {
    case 0U:
      lane.x = values[i];
      break;
    case 1U:
      lane.y = values[i];
      break;
    case 2U:
      lane.z = values[i];
      break;
    default:
      lane.w = values[i];
      break;
    }
  }
  return packed;
}

} // namespace oxygen::engine::detail
