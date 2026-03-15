//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>
#include <Oxygen/Renderer/Types/VirtualShadowPhysicalPageMetadata.h>

namespace oxygen::graphics {
class Buffer;
}

namespace oxygen::renderer {

enum class VirtualPageResidencyState : std::uint8_t {
  kUnmapped = 0U,
  kResidentClean = 1U,
  kResidentDirty = 2U,
  kPendingRender = 3U,
};

enum class VirtualShadowAtlasTileDebugState : std::uint32_t {
  kCleared = 0U,
  kCached = 1U,
  kReused = 2U,
  kRewritten = 3U,
};

enum class DirectionalVirtualClipCacheStatus : std::uint8_t {
  kNoPreviousFrame = 0U,
  kNeverRendered = 1U,
  kForceInvalidated = 2U,
  kLayoutInvalid = 3U,
  kDepthGuardbandInvalid = 4U,
  kPanningDisabled = 5U,
  kReuseGuardbandInvalid = 6U,
  kValid = 7U,
};

struct DirectionalVirtualCacheControls {
  bool clipmap_panning_enabled { true };
  bool force_invalidate { false };
};

struct alignas(16) VirtualShadowResolveStats {
  std::uint32_t resident_entry_count { 0U };
  std::uint32_t resident_entry_capacity { 0U };
  std::uint32_t clean_page_count { 0U };
  std::uint32_t dirty_page_count { 0U };
  std::uint32_t pending_page_count { 0U };
  std::uint32_t mapped_page_count { 0U };
  std::uint32_t pending_raster_page_count { 0U };
  std::uint32_t selected_page_count { 0U };
  std::uint32_t allocated_page_count { 0U };
  std::uint32_t evicted_page_count { 0U };
  std::uint32_t rerasterized_page_count { 0U };
  std::uint32_t reused_requested_page_count { 0U };
  std::uint32_t requested_page_list_count { 0U };
  std::uint32_t dirty_page_list_count { 0U };
  std::uint32_t clean_page_list_count { 0U };
  std::uint32_t available_page_list_count { 0U };
};
static_assert(sizeof(VirtualShadowResolveStats) % 16U == 0U);

struct alignas(16) VirtualShadowDirtyResidentPageEntry {
  std::uint64_t resident_key { 0U };
  std::uint32_t page_flags { 0U };
  std::uint32_t _pad0 { 0U };
};
static_assert(sizeof(VirtualShadowDirtyResidentPageEntry) % 16U == 0U);

struct VirtualShadowPageManagementBindings {
  ShaderVisibleIndex page_table_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_table_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_flags_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_flags_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_metadata_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_metadata_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_lists_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_lists_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex resolve_stats_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex resolve_stats_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex dirty_resident_pages_srv { kInvalidShaderVisibleIndex };
  std::uint32_t physical_page_capacity { 0U };
  std::uint32_t dirty_resident_page_count { 0U };
  bool global_dirty_resident_contents { false };
};

// Live GPU-authored raster inputs produced by the resolve pass and consumed by
// the page raster pass in the same frame.
struct VirtualShadowGpuRasterInputs {
  std::shared_ptr<graphics::Buffer> schedule_buffer {};
  ShaderVisibleIndex schedule_srv { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> schedule_count_buffer {};
  ShaderVisibleIndex schedule_count_srv { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> draw_page_ranges_buffer {};
  ShaderVisibleIndex draw_page_ranges_srv { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> draw_page_indices_buffer {};
  ShaderVisibleIndex draw_page_indices_srv { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> clear_indirect_args_buffer {};
  std::shared_ptr<graphics::Buffer> draw_indirect_args_buffer {};

  frame::SequenceNumber source_frame_sequence { 0U };
  std::uint32_t draw_count { 0U };
  std::uint32_t schedule_capacity { 0U };
};

struct VirtualShadowViewIntrospection {
  std::span<const engine::DirectionalVirtualShadowMetadata>
    directional_virtual_metadata {};
  std::span<const engine::DirectionalVirtualShadowMetadata>
    published_directional_virtual_metadata {};
  std::span<const std::uint32_t> page_table_entries {};
  std::span<const std::uint32_t> published_page_table_entries {};
  std::span<const std::uint32_t> page_flags_entries {};
  std::span<const std::uint32_t> published_page_flags_entries {};
  std::span<const VirtualShadowPhysicalPageMetadata>
    physical_page_metadata_entries {};
  std::span<const VirtualShadowPhysicalPageListEntry>
    physical_page_list_entries {};
  std::span<const std::int32_t> clipmap_page_offset_x {};
  std::span<const std::int32_t> clipmap_page_offset_y {};
  std::span<const bool> clipmap_reuse_guardband_valid {};
  std::span<const bool> clipmap_cache_valid {};
  std::span<const DirectionalVirtualClipCacheStatus> clipmap_cache_status {};
  std::span<const std::uint32_t> atlas_tile_debug_states {};
  bool used_request_feedback { false };
  bool cache_layout_compatible { false };
  bool depth_guardband_valid { false };
  bool has_persistent_gpu_residency_state { false };
  ShaderVisibleIndex resolve_stats_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_metadata_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_lists_srv { kInvalidShaderVisibleIndex };
  std::uint32_t mapped_page_count { 0U };
  std::uint32_t resident_page_count { 0U };
  std::uint32_t clean_page_count { 0U };
  std::uint32_t dirty_page_count { 0U };
  std::uint32_t pending_page_count { 0U };
  std::uint32_t pending_raster_page_count { 0U };
  std::uint32_t selected_page_count { 0U };
  std::uint32_t coarse_backbone_page_count { 0U };
  std::uint32_t coarse_safety_selected_page_count { 0U };
  std::uint32_t coarse_safety_budget_page_count { 0U };
  bool coarse_safety_capacity_fit { false };
  std::uint32_t same_frame_detail_page_count { 0U };
  std::uint32_t feedback_requested_page_count { 0U };
  std::uint32_t feedback_refinement_page_count { 0U };
  std::uint32_t receiver_bootstrap_page_count { 0U };
  std::uint32_t current_frame_reinforcement_page_count { 0U };
  std::uint64_t current_frame_reinforcement_reference_frame { 0U };
  std::uint32_t allocated_page_count { 0U };
  std::uint32_t evicted_page_count { 0U };
  std::uint32_t rerasterized_page_count { 0U };
  std::uint32_t requested_page_list_count { 0U };
  std::uint32_t dirty_page_list_count { 0U };
  std::uint32_t clean_page_list_count { 0U };
  std::uint32_t available_page_list_count { 0U };
  VirtualShadowResolveStats resolve_stats {};
};

} // namespace oxygen::renderer
