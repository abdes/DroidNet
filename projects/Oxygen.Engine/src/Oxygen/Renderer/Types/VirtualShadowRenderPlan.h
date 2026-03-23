//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <glm/mat4x4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>
#include <Oxygen/Renderer/Types/ShadowFramePublication.h>

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

struct alignas(16) VirtualShadowResolveStats {
  std::uint32_t scheduled_raster_page_count { 0U };
  std::uint32_t allocated_page_count { 0U };
  std::uint32_t requested_page_count { 0U };
  std::uint32_t resident_dirty_page_count { 0U };
  std::uint32_t resident_clean_page_count { 0U };
  std::uint32_t pages_requiring_schedule_count { 0U };
  std::uint32_t available_page_list_count { 0U };
  std::uint32_t rasterized_page_count { 0U };
  std::uint32_t cached_page_transition_count { 0U };
  std::uint32_t reserved0 { 0U };
  std::uint32_t reserved1 { 0U };
  std::uint32_t reserved2 { 0U };
};
static_assert(sizeof(VirtualShadowResolveStats) % 16U == 0U);

struct VirtualShadowPageManagementBindings {
  ShaderVisibleIndex page_table_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_table_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_flags_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_flags_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex dirty_page_flags_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex previous_shadow_caster_bounds_srv {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex current_shadow_caster_bounds_srv {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex physical_page_metadata_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_metadata_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_lists_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_page_lists_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex resolve_stats_uav { kInvalidShaderVisibleIndex };
  glm::mat4 previous_light_view { 1.0F };
  std::uint32_t shadow_caster_bound_count { 0U };
  std::uint32_t physical_page_capacity { 0U };
  std::uint32_t atlas_tiles_per_axis { 0U };
  // These flags cross the renderer/test module boundary and are copied into
  // shader pass constants, so keep them as fixed-width values.
  std::uint32_t reset_page_management_state { 0U };
  std::uint32_t global_dirty_resident_contents { 0U };
};

struct VirtualShadowPageManagementStateSnapshot {
  std::uint32_t reset_page_management_state { 0U };
  std::uint32_t reset_request_pending { 0U };
  std::uint32_t global_dirty_resident_contents { 0U };
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

  std::shared_ptr<graphics::Buffer> draw_page_counter_buffer {};

  std::shared_ptr<graphics::Buffer> clear_indirect_args_buffer {};
  std::shared_ptr<graphics::Buffer> draw_indirect_args_buffer {};

  frame::SequenceNumber source_frame_sequence { 0U };
  std::uint32_t draw_count { 0U };
  std::uint64_t cache_epoch { 0U };
  std::uint64_t view_generation { 0U };
};

//! Authoritative per-view VSM packet for the current frame/generation.
/*!
 Internal VSM passes must consume this packet rather than rediscovering
 metadata/bindings through published view-frame state. That keeps request,
 schedule, build, raster, finalize, and lighting publication on one authority.
*/
struct VirtualShadowFramePacket {
  frame::SequenceNumber source_frame_sequence { 0U };
  std::uint64_t cache_epoch { 0U };
  std::uint64_t view_generation { 0U };

  ShadowFramePublication publication {};
  ShaderVisibleIndex virtual_directional_shadow_metadata_srv {
    kInvalidShaderVisibleIndex
  };
  engine::DirectionalVirtualShadowMetadata directional_metadata {};
  bool has_directional_metadata { false };

  VirtualShadowPageManagementBindings page_management_bindings {};
  bool has_page_management_bindings { false };

  VirtualShadowPageManagementStateSnapshot page_management_state {};
  bool has_page_management_state { false };

  std::shared_ptr<graphics::Buffer> page_flags_buffer {};
  std::shared_ptr<graphics::Buffer> physical_page_metadata_buffer {};
  std::shared_ptr<graphics::Buffer> resolve_stats_buffer {};

  VirtualShadowGpuRasterInputs gpu_raster_inputs {};
  bool has_gpu_raster_inputs { false };
};

} // namespace oxygen::renderer
