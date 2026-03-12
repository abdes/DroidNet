//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

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

// Bridge payload for the current resolve-to-raster transition. These entries
// mirror the backend-private residency snapshot uploaded into persistent GPU
// buffers until the dedicated resolve pass becomes the only author of page
// scheduling.
struct alignas(16) VirtualShadowResolveResidentPageEntry {
  std::uint64_t resident_key { 0U };
  std::uint32_t atlas_tile_x { 0U };
  std::uint32_t atlas_tile_y { 0U };
  std::uint32_t residency_state { 0U };
  std::uint32_t _pad0 { 0U };
  std::uint64_t last_touched_frame { 0U };
  std::uint64_t last_requested_frame { 0U };
};
static_assert(sizeof(VirtualShadowResolveResidentPageEntry) % 16U == 0U);

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
};
static_assert(sizeof(VirtualShadowResolveStats) % 16U == 0U);

// Authoritative page-raster contract consumed by the virtual page raster pass.
// The explicit resolve stage is the only author of this payload.
struct VirtualShadowResolvedRasterPage {
  std::uint32_t shadow_instance_index { 0xFFFFFFFFU };
  std::uint32_t payload_index { 0xFFFFFFFFU };
  std::uint32_t clip_level { 0U };
  std::uint32_t page_index { 0U };
  std::uint64_t resident_key { 0U };
  std::uint16_t atlas_tile_x { 0U };
  std::uint16_t atlas_tile_y { 0U };
  engine::ViewConstants::GpuData view_constants {};
};

struct VirtualShadowRenderPlan {
  const graphics::Texture* depth_texture { nullptr };
  std::span<const VirtualShadowResolvedRasterPage> resolved_pages {};
  std::uint32_t page_size_texels { 0U };
  std::uint32_t atlas_tiles_per_axis { 0U };
};

struct VirtualShadowViewIntrospection {
  std::span<const engine::DirectionalVirtualShadowMetadata>
    directional_virtual_metadata {};
  std::span<const engine::DirectionalVirtualShadowMetadata>
    published_directional_virtual_metadata {};
  std::span<const VirtualShadowResolvedRasterPage> resolved_raster_pages {};
  std::span<const VirtualShadowResolveResidentPageEntry>
    resolve_resident_page_entries {};
  std::span<const std::uint32_t> page_table_entries {};
  std::span<const std::uint32_t> published_page_table_entries {};
  std::span<const std::uint32_t> atlas_tile_debug_states {};
  bool used_request_feedback { false };
  bool used_resolved_raster_schedule { false };
  bool used_last_coherent_publish_fallback { false };
  bool last_coherent_publish_compatible { false };
  bool has_persistent_gpu_residency_state { false };
  ShaderVisibleIndex resolve_resident_pages_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex resolve_stats_srv { kInvalidShaderVisibleIndex };
  std::uint32_t mapped_page_count { 0U };
  std::uint32_t resident_page_count { 0U };
  std::uint32_t clean_page_count { 0U };
  std::uint32_t dirty_page_count { 0U };
  std::uint32_t pending_page_count { 0U };
  std::uint32_t pending_raster_page_count { 0U };
  std::uint32_t resolved_schedule_page_count { 0U };
  std::uint32_t resolved_schedule_pruned_job_count { 0U };
  std::uint32_t selected_page_count { 0U };
  std::uint32_t coarse_backbone_page_count { 0U };
  std::uint32_t coarse_safety_selected_page_count { 0U };
  std::uint32_t coarse_safety_budget_page_count { 0U };
  bool coarse_safety_capacity_fit { false };
  std::uint32_t feedback_requested_page_count { 0U };
  std::uint32_t feedback_refinement_page_count { 0U };
  std::uint32_t receiver_bootstrap_page_count { 0U };
  std::uint32_t current_frame_reinforcement_page_count { 0U };
  std::uint64_t current_frame_reinforcement_reference_frame { 0U };
  std::uint64_t resolved_schedule_age_frames { 0U };
  std::uint64_t last_coherent_publish_age_frames { 0U };
  std::uint32_t incoherent_publish_frame_count { 0U };
  std::uint32_t allocated_page_count { 0U };
  std::uint32_t evicted_page_count { 0U };
  std::uint32_t rerasterized_page_count { 0U };
  VirtualShadowResolveStats resolve_stats {};
};

} // namespace oxygen::renderer
