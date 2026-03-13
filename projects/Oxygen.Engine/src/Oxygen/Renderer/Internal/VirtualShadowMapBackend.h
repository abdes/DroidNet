//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Types/DirectionalShadowCandidate.h>
#include <Oxygen/Renderer/Types/ShadowFramePublication.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageFlags.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageTableEntry.h>
#include <Oxygen/Renderer/Types/VirtualShadowPhysicalPageMetadata.h>
#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>
#include <Oxygen/Renderer/Types/VirtualShadowRequestFeedback.h>
#include <Oxygen/Renderer/Types/VirtualShadowResolvedRasterSchedule.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>


namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
class StagingProvider;
}

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::renderer::internal {

class VirtualShadowMapBackend {
public:
  OXGN_RNDR_API VirtualShadowMapBackend(::oxygen::Graphics* gfx,
    ::oxygen::engine::upload::StagingProvider* provider,
    ::oxygen::engine::upload::InlineTransfersCoordinator* inline_transfers,
    ::oxygen::ShadowQualityTier quality_tier);

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowMapBackend)
  OXYGEN_MAKE_NON_MOVABLE(VirtualShadowMapBackend)

  OXGN_RNDR_API ~VirtualShadowMapBackend();

  OXGN_RNDR_API auto OnFrameStart(
    RendererTag tag, frame::SequenceNumber sequence, frame::Slot slot) -> void;

  OXGN_RNDR_API auto PublishView(ViewId view_id,
    const engine::ViewConstants& view_constants,
    std::span<const engine::DirectionalShadowCandidate> directional_candidates,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    std::chrono::milliseconds gpu_budget, bool allow_budget_fallback = true,
    std::uint64_t shadow_caster_content_hash = 0U) -> ShadowFramePublication;
  OXGN_RNDR_API auto ResolveCurrentFrame(ViewId view_id) -> void;
  OXGN_RNDR_API auto MarkRendered(ViewId view_id) -> void;
  OXGN_RNDR_API auto PreparePageTableResources(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto PreparePageManagementOutputsForGpuWrite(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto FinalizePageManagementOutputs(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto SetPublishedViewFrameBindingsSlot(
    ViewId view_id, engine::BindlessViewFrameBindingsSlot slot) -> void;
  OXGN_RNDR_API auto SubmitRequestFeedback(
    ViewId view_id, VirtualShadowRequestFeedback feedback) -> void;
  OXGN_RNDR_API auto ClearRequestFeedback(ViewId view_id,
    VirtualShadowFeedbackKind kind = VirtualShadowFeedbackKind::kDetail)
    -> void;
  OXGN_RNDR_API auto SubmitResolvedRasterSchedule(
    ViewId view_id, VirtualShadowResolvedRasterSchedule schedule) -> void;
  OXGN_RNDR_API auto ClearResolvedRasterSchedule(ViewId view_id) -> void;
  OXGN_RNDR_API auto SetDirectionalCacheControls(
    renderer::DirectionalVirtualCacheControls controls) -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetDirectionalCacheControls() const noexcept
    -> renderer::DirectionalVirtualCacheControls;

  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetFramePublication(
    ViewId view_id) const noexcept -> const ShadowFramePublication*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetRenderPlan(
    ViewId view_id) const noexcept -> const VirtualShadowRenderPlan*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetViewIntrospection(
    ViewId view_id) const noexcept -> const VirtualShadowViewIntrospection*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetPageManagementBindings(
    ViewId view_id) const noexcept
    -> const renderer::VirtualShadowPageManagementBindings*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetDirectionalVirtualMetadata(
    ViewId view_id) const noexcept
    -> const engine::DirectionalVirtualShadowMetadata*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPhysicalPoolTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;

private:
  struct PhysicalPoolConfig {
    std::uint32_t page_size_texels { 0U };
    std::uint32_t virtual_pages_per_clip_axis { 0U };
    std::uint32_t clip_level_count { 0U };
    std::uint32_t virtual_page_count { 0U };
    std::uint32_t physical_tile_capacity { 0U };
    std::uint32_t atlas_tiles_per_axis { 0U };
    std::uint32_t atlas_resolution { 0U };
  };

  struct PublicationKey {
    std::uint64_t view_hash { 0U };
    std::uint64_t candidate_hash { 0U };
    std::uint64_t caster_hash { 0U };
    std::uint64_t shadow_content_hash { 0U };

    [[nodiscard]] auto operator==(const PublicationKey&) const noexcept -> bool
      = default;
  };

  struct PhysicalTileAddress {
    std::uint16_t tile_x { 0U };
    std::uint16_t tile_y { 0U };
  };

  struct ResidentVirtualPage {
    PhysicalTileAddress tile {};
    renderer::VirtualPageResidencyState state {
      renderer::VirtualPageResidencyState::kUnmapped
    };
    frame::SequenceNumber last_touched_frame { 0U };
    frame::SequenceNumber last_requested_frame { 0U };

    [[nodiscard]] auto ContentsValid() const noexcept -> bool
    {
      return state == renderer::VirtualPageResidencyState::kResidentClean;
    }
  };

  struct AbsoluteClipPageRegion {
    bool valid { false };
    std::int32_t min_x { 0 };
    std::int32_t max_x { 0 };
    std::int32_t min_y { 0 };
    std::int32_t max_y { 0 };
  };

  struct ClipSelectedRegion {
    bool valid { false };
    std::uint32_t min_x { 0U };
    std::uint32_t max_x { 0U };
    std::uint32_t min_y { 0U };
    std::uint32_t max_y { 0U };
  };

  struct DirectionalVirtualClipmapSetup {
    bool valid { false };
    bool cache_layout_compatible { false };
    bool depth_guardband_valid { false };
    std::uint32_t clip_level_count { 0U };
    std::uint32_t pages_per_axis { 0U };
    std::uint32_t pages_per_level { 0U };
    engine::ShadowInstanceMetadata shadow_instance {};
    engine::DirectionalVirtualShadowMetadata metadata {};
    glm::mat4 light_view { 1.0F };
    glm::vec3 light_eye { 0.0F, 0.0F, 0.0F };
    float near_plane { 0.0F };
    float far_plane { 0.0F };
    std::array<float, engine::kMaxVirtualDirectionalClipLevels>
      clip_page_world {};
    std::array<float, engine::kMaxVirtualDirectionalClipLevels>
      clip_origin_x {};
    std::array<float, engine::kMaxVirtualDirectionalClipLevels>
      clip_origin_y {};
    std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
      clip_grid_origin_x {};
    std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
      clip_grid_origin_y {};
    std::array<ClipSelectedRegion, engine::kMaxVirtualDirectionalClipLevels>
      frustum_regions {};
    std::vector<AbsoluteClipPageRegion> absolute_frustum_regions {};
    std::uint32_t coarse_safety_clip_index { 0U };
    std::uint32_t coarse_safety_budget_pages { 0U };
    glm::vec2 coarse_safety_priority_center_ls { 0.0F, 0.0F };
    bool coarse_safety_priority_valid { false };
    std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
      previous_clip_page_offset_x {};
    std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
      previous_clip_page_offset_y {};
    std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
      previous_clip_reuse_guardband_valid {};
    std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
      previous_clip_cache_valid {};
    std::array<renderer::DirectionalVirtualClipCacheStatus,
      engine::kMaxVirtualDirectionalClipLevels>
      previous_clip_cache_status {};
  };

  struct ViewCacheEntry {
    struct PendingResidentReuseGateSnapshot {
      bool valid { false };
      bool previous_pending_resolved_pages_empty { false };
      PublicationKey key {};
      std::vector<engine::DirectionalVirtualShadowMetadata>
        directional_virtual_metadata {};
      std::vector<std::uint32_t> page_table_entries {};
    };

    struct PendingResidencyResolve {
      bool valid { false };
      bool dirty { false };
      std::uint32_t clip_level_count { 0U };
      std::uint32_t pages_per_axis { 0U };
      std::uint32_t pages_per_level { 0U };
      engine::ViewConstants view_constants {};
      glm::mat4 light_view { 1.0F };
      glm::vec3 light_eye { 0.0F, 0.0F, 0.0F };
      float near_plane { 0.0F };
      float far_plane { 0.0F };
      std::array<float, engine::kMaxVirtualDirectionalClipLevels>
        clip_page_world {};
      std::array<float, engine::kMaxVirtualDirectionalClipLevels>
        clip_origin_x {};
      std::array<float, engine::kMaxVirtualDirectionalClipLevels>
        clip_origin_y {};
      std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
        clip_grid_origin_x {};
      std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
        clip_grid_origin_y {};
      std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
        previous_clip_page_offset_x {};
      std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
        previous_clip_page_offset_y {};
      std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
        previous_clip_reuse_guardband_valid {};
      std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
        previous_clip_cache_valid {};
      std::array<renderer::DirectionalVirtualClipCacheStatus,
        engine::kMaxVirtualDirectionalClipLevels>
        previous_clip_cache_status {};
      bool cache_layout_compatible { false };
      bool depth_guardband_valid { false };
      std::uint32_t coarse_backbone_begin { 0U };
      std::uint32_t coarse_safety_clip_index { 0U };
      std::uint32_t coarse_safety_max_page_count { 0U };
      glm::vec2 coarse_safety_priority_center_ls { 0.0F, 0.0F };
      bool coarse_safety_priority_valid { false };
      std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
        reusable_clip_contents {};
      bool address_space_compatible { false };
      bool global_dirty_resident_contents { false };
      std::vector<std::uint8_t> selected_pages {};
      std::unordered_map<std::uint64_t, ResidentVirtualPage>
        previous_resident_pages {};
      std::vector<glm::vec4> previous_shadow_caster_bounds {};
      std::unordered_set<std::uint64_t> dirty_resident_pages {};
      PendingResidentReuseGateSnapshot resident_reuse_snapshot {};
    };

    enum class RequestFeedbackDecision : std::uint8_t {
      kNoFeedback,
      kEmptyFeedback,
      kDimensionMismatch,
      kAddressSpaceMismatch,
      kSameFrame,
      kStale,
      kAccepted,
    };

    struct PublishDiagnostics {
      RequestFeedbackDecision feedback_decision {
        RequestFeedbackDecision::kNoFeedback
      };
      std::uint32_t feedback_key_count { 0U };
      std::uint64_t feedback_age_frames { 0U };
      bool address_space_compatible { false };
      bool cache_layout_compatible { false };
      bool depth_guardband_valid { false };
      bool global_dirty_resident_contents { false };
      std::uint32_t shadow_caster_bound_count { 0U };
      std::uint32_t visible_receiver_bound_count { 0U };
      std::uint32_t clip_level_count { 0U };
      std::uint32_t coarse_backbone_begin { 0U };
      std::uint32_t selected_page_count { 0U };
      std::uint32_t coarse_backbone_pages { 0U };
      std::uint32_t coarse_safety_selected_pages { 0U };
      std::uint32_t coarse_safety_budget_pages { 0U };
      bool coarse_safety_capacity_fit { false };
      bool predicted_coherent_publication { false };
      std::uint32_t feedback_requested_pages { 0U };
      std::uint32_t feedback_refinement_pages { 0U };
      std::uint32_t receiver_bootstrap_pages { 0U };
      std::uint32_t current_frame_reinforcement_pages { 0U };
      std::uint64_t current_frame_reinforcement_reference_frame { 0U };
      std::uint32_t resolved_schedule_pages { 0U };
      std::uint32_t resolved_schedule_pruned_jobs { 0U };
      std::uint64_t resolved_schedule_age_frames { 0U };
      bool used_resolved_raster_schedule { false };
      std::uint32_t previous_resident_pages { 0U };
      std::uint32_t carried_resident_pages { 0U };
      std::uint32_t released_resident_pages { 0U };
      std::uint32_t dirty_resident_page_count { 0U };
      std::uint32_t marked_dirty_pages { 0U };
      std::uint32_t reused_requested_pages { 0U };
      std::uint32_t allocated_pages { 0U };
      std::uint32_t evicted_pages { 0U };
      std::uint32_t allocation_failures { 0U };
      std::uint32_t rerasterized_pages { 0U };
      bool resident_reuse_gate_open { false };
    };

    PublicationKey key {};
    std::vector<engine::ShadowInstanceMetadata> shadow_instances;
    std::vector<engine::DirectionalVirtualShadowMetadata>
      directional_virtual_metadata;
    std::vector<AbsoluteClipPageRegion> absolute_frustum_regions;
    std::vector<glm::vec4> shadow_caster_bounds;
    std::vector<std::uint32_t> page_table_entries;
    std::vector<std::uint32_t> page_flags_entries;
    std::vector<std::uint32_t> atlas_tile_debug_states;
    std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
      clipmap_page_offset_x {};
    std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
      clipmap_page_offset_y {};
    std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
      clipmap_reuse_guardband_valid {};
    std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
      clipmap_cache_valid {};
    std::array<renderer::DirectionalVirtualClipCacheStatus,
      engine::kMaxVirtualDirectionalClipLevels>
      clipmap_cache_status {};
    std::vector<std::uint64_t> compatible_feedback_address_space_hashes {};
    std::vector<renderer::VirtualShadowResolveResidentPageEntry>
      resolve_resident_page_entries;
    std::vector<renderer::VirtualShadowPhysicalPageMetadata>
      physical_page_metadata_entries;
    std::vector<renderer::VirtualShadowPhysicalPageListEntry>
      physical_page_list_entries;
    std::vector<renderer::VirtualShadowResolvedRasterPage>
      resolved_raster_pages;
    bool has_rendered_cache_history { false };
    PendingResidencyResolve pending_residency_resolve {};
    std::unordered_map<std::uint64_t, ResidentVirtualPage> resident_pages;
    renderer::VirtualShadowResolveStats resolve_stats {};
    PublishDiagnostics publish_diagnostics {};
    ShadowFramePublication frame_publication {};
    renderer::VirtualShadowPageManagementBindings page_management_bindings {};
    VirtualShadowRenderPlan render_plan {};
    VirtualShadowViewIntrospection introspection {};
  };

  struct PendingRequestFeedbackChannel {
    VirtualShadowRequestFeedback feedback {};
    std::vector<AbsoluteClipPageRegion> source_absolute_frustum_regions {};
    bool valid { false };
  };

  struct PendingRequestFeedback {
    PendingRequestFeedbackChannel detail {};
    PendingRequestFeedbackChannel coarse {};

    [[nodiscard]] auto Empty() const noexcept -> bool
    {
      return !detail.valid && !coarse.valid;
    }
  };

  ::oxygen::Graphics* gfx_ { nullptr };
  ::oxygen::engine::upload::StagingProvider* staging_provider_ { nullptr };
  ::oxygen::engine::upload::InlineTransfersCoordinator* inline_transfers_ {
    nullptr
  };
  ::oxygen::ShadowQualityTier shadow_quality_tier_ {
    ::oxygen::ShadowQualityTier::kHigh
  };
  frame::SequenceNumber frame_sequence_ { 0U };
  frame::Slot frame_slot_ { frame::kInvalidSlot };

  using BufferT = engine::upload::TransientStructuredBuffer;
  BufferT shadow_instance_buffer_;
  BufferT directional_virtual_metadata_buffer_;

  struct ViewStructuredWordBufferResources {
    std::shared_ptr<graphics::Buffer> gpu_buffer;
    std::shared_ptr<graphics::Buffer> upload_buffer;
    std::uint32_t* mapped_upload { nullptr };
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav { kInvalidShaderVisibleIndex };
    std::uint32_t entry_capacity { 0U };
  };

  struct ViewResolveResources {
    std::shared_ptr<graphics::Buffer> resident_pages_gpu_buffer;
    std::shared_ptr<graphics::Buffer> resident_pages_upload_buffer;
    renderer::VirtualShadowResolveResidentPageEntry*
      mapped_resident_pages_upload { nullptr };
    ShaderVisibleIndex resident_pages_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex resident_pages_uav { kInvalidShaderVisibleIndex };
    std::uint32_t resident_page_capacity { 0U };
    std::uint32_t resident_page_upload_count { 0U };
    bool resident_page_upload_pending { false };

    std::shared_ptr<graphics::Buffer> stats_gpu_buffer;
    std::shared_ptr<graphics::Buffer> stats_upload_buffer;
    renderer::VirtualShadowResolveStats* mapped_stats_upload { nullptr };
    ShaderVisibleIndex stats_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex stats_uav { kInvalidShaderVisibleIndex };
    bool stats_upload_pending { false };

    std::shared_ptr<graphics::Buffer> physical_page_metadata_gpu_buffer;
    std::shared_ptr<graphics::Buffer> physical_page_metadata_upload_buffer;
    renderer::VirtualShadowPhysicalPageMetadata*
      mapped_physical_page_metadata_upload { nullptr };
    ShaderVisibleIndex physical_page_metadata_srv {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t physical_page_metadata_capacity { 0U };
    std::uint32_t physical_page_metadata_upload_count { 0U };
    bool physical_page_metadata_upload_pending { false };

    std::shared_ptr<graphics::Buffer> physical_page_lists_gpu_buffer;
    std::shared_ptr<graphics::Buffer> physical_page_lists_upload_buffer;
    renderer::VirtualShadowPhysicalPageListEntry*
      mapped_physical_page_lists_upload { nullptr };
    ShaderVisibleIndex physical_page_lists_srv { kInvalidShaderVisibleIndex };
    std::uint32_t physical_page_lists_capacity { 0U };
    std::uint32_t physical_page_lists_upload_count { 0U };
    bool physical_page_lists_upload_pending { false };
  };

  std::shared_ptr<graphics::Texture> physical_pool_texture_;
  graphics::NativeView physical_pool_srv_view_ {};
  ShaderVisibleIndex physical_pool_srv_ { kInvalidShaderVisibleIndex };
  PhysicalPoolConfig physical_pool_config_ {};
  std::vector<PhysicalTileAddress> free_physical_tiles_;
  renderer::DirectionalVirtualCacheControls directional_cache_controls_ {};

  std::unordered_map<ViewId, ViewCacheEntry> view_cache_;
  std::unordered_map<ViewId, ViewStructuredWordBufferResources>
    view_page_table_resources_;
  std::unordered_map<ViewId, ViewStructuredWordBufferResources>
    view_page_flags_resources_;
  std::unordered_map<ViewId, ViewStructuredWordBufferResources>
    view_page_management_page_table_resources_;
  std::unordered_map<ViewId, ViewStructuredWordBufferResources>
    view_page_management_page_flags_resources_;
  std::unordered_map<ViewId, ViewResolveResources> view_resolve_resources_;
  std::unordered_map<ViewId, PendingRequestFeedback> request_feedback_;
  std::unordered_map<ViewId, VirtualShadowResolvedRasterSchedule>
    resolved_raster_schedules_;

  struct ViewPublishLogState {
    std::uint32_t last_selected_page_count { 0U };
    std::uint32_t last_pending_raster_page_count { 0U };
    bool last_used_feedback { false };
    bool initialized { false };
  };
  std::unordered_map<ViewId, ViewPublishLogState> publish_log_states_;

  struct CoherenceReadbackSlot {
    std::shared_ptr<graphics::Buffer> page_table_readback;
    std::shared_ptr<graphics::Buffer> page_flags_readback;
    const std::uint32_t* mapped_page_table { nullptr };
    const std::uint32_t* mapped_page_flags { nullptr };
    std::vector<std::uint32_t> cpu_page_table_snapshot;
    std::vector<std::uint32_t> cpu_page_flags_snapshot;
    frame::SequenceNumber source_frame { 0U };
    ViewId view_id {};
    std::uint32_t entry_count { 0U };
    bool live_authority { false };
    bool pending { false };
  };
  std::array<CoherenceReadbackSlot, frame::kFramesInFlight.get()>
    coherence_readbacks_ {};

  OXGN_RNDR_API auto BuildPublicationKey(
    const engine::ViewConstants& view_constants,
    std::span<const engine::DirectionalShadowCandidate> directional_candidates,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::uint64_t shadow_caster_content_hash) const -> PublicationKey;
  OXGN_RNDR_API auto RebuildResolveStateSnapshot(ViewCacheEntry& state) const
    -> void;
  OXGN_RNDR_API auto PropagateDirectionalHierarchicalPageFlags(
    ViewCacheEntry& state,
    const ViewCacheEntry::PendingResidencyResolve& pending) const -> void;
  OXGN_RNDR_API auto RebuildPhysicalPageManagementSnapshot(
    ViewCacheEntry& state,
    const ViewCacheEntry::PendingResidencyResolve& pending) const -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto BuildRequestedResidentKeySet(
    const ViewCacheEntry::PendingResidencyResolve& pending) const
    -> std::unordered_set<std::uint64_t>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto BuildEvictionCandidateList(
    const ViewCacheEntry& state,
    const std::unordered_set<std::uint64_t>& protected_resident_keys) const
    -> std::vector<std::uint64_t>;
  OXGN_RNDR_API auto ResolvePendingPageResidency(ViewId view_id) -> void;
  OXGN_RNDR_API auto CarryForwardCompatibleDirectionalResidentPages(
    ViewCacheEntry& state,
    const ViewCacheEntry::PendingResidencyResolve& pending,
    std::uint32_t& released_page_count, std::uint32_t& marked_dirty_page_count)
    -> void;
  OXGN_RNDR_API auto PopulateDirectionalFallbackPageTableEntries(
    ViewCacheEntry& state,
    const ViewCacheEntry::PendingResidencyResolve& pending) -> void;
  OXGN_RNDR_API auto RefreshViewExports(
    ViewId view_id, ViewCacheEntry& state) const -> void;
  OXGN_RNDR_API auto RefreshAtlasTileDebugStates(ViewCacheEntry& state) const
    -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto CanReuseResidentPages(
    const ViewCacheEntry& previous,
    const ViewCacheEntry& current) const noexcept -> bool;
  [[nodiscard]] OXGN_RNDR_NDAPI auto CanReuseResidentPages(
    const ViewCacheEntry::PendingResidentReuseGateSnapshot& previous,
    const ViewCacheEntry& current) const noexcept -> bool;
  OXGN_RNDR_API auto BuildPhysicalPoolConfig(
    const engine::DirectionalShadowCandidate& candidate,
    std::chrono::milliseconds gpu_budget, std::size_t shadow_caster_count) const
    -> PhysicalPoolConfig;
  OXGN_RNDR_API auto EnsurePhysicalPool(const PhysicalPoolConfig& config)
    -> void;
  OXGN_RNDR_API auto ReleasePhysicalPool() -> void;
  OXGN_RNDR_API auto AllocatePhysicalTile()
    -> std::optional<PhysicalTileAddress>;
  OXGN_RNDR_API auto AcquirePhysicalTile(ViewCacheEntry& state,
    std::vector<std::uint64_t>& eviction_candidates,
    std::size_t& next_eviction_candidate_index,
    std::uint32_t& evicted_page_count) -> std::optional<PhysicalTileAddress>;
  OXGN_RNDR_API auto ReleasePhysicalTile(PhysicalTileAddress tile) -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto PrepareDirectionalVirtualClipmapSetup(
    const engine::ViewConstants& view_constants,
    const engine::DirectionalShadowCandidate& candidate,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    const ViewCacheEntry* previous_state) const
    -> std::optional<DirectionalVirtualClipmapSetup>;
  OXGN_RNDR_API auto BuildDirectionalVirtualViewState(ViewId view_id,
    const engine::ViewConstants& view_constants,
    const engine::DirectionalShadowCandidate& candidate,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    const ViewCacheEntry* previous_state, ViewCacheEntry& state) -> void;
  OXGN_RNDR_API auto LogPublishTransition(
    ViewId view_id, const ViewCacheEntry& state) -> void;

  OXGN_RNDR_API auto PublishShadowInstances(
    std::span<const engine::ShadowInstanceMetadata> instances)
    -> ShaderVisibleIndex;
  OXGN_RNDR_API auto PublishDirectionalVirtualMetadata(
    std::span<const engine::DirectionalVirtualShadowMetadata> metadata)
    -> ShaderVisibleIndex;
  // Bridge hook until the dedicated GPU allocation/update path lands: keep
  // persistent GPU page-table and backend-private resolve-state resources
  // synchronized while virtual raster consumes the authoritative resolved-page
  // contract.
  OXGN_RNDR_API auto EnsureViewPageTableResources(ViewId view_id,
    std::uint32_t required_entry_count) -> ViewStructuredWordBufferResources*;
  OXGN_RNDR_API auto EnsurePageTablePublication(
    ViewId view_id, std::uint32_t required_entry_count) -> ShaderVisibleIndex;
  OXGN_RNDR_API auto EnsureViewPageFlagResources(ViewId view_id,
    std::uint32_t required_entry_count) -> ViewStructuredWordBufferResources*;
  OXGN_RNDR_API auto EnsurePageFlagsPublication(
    ViewId view_id, std::uint32_t required_entry_count) -> ShaderVisibleIndex;
  OXGN_RNDR_API auto EnsureViewPageManagementPageTableResources(ViewId view_id,
    std::uint32_t required_entry_count) -> ViewStructuredWordBufferResources*;
  OXGN_RNDR_API auto EnsureViewPageManagementPageFlagResources(ViewId view_id,
    std::uint32_t required_entry_count) -> ViewStructuredWordBufferResources*;
  auto EnsureCoherenceReadbackBuffers(
    CoherenceReadbackSlot& slot, std::uint64_t size_bytes) -> bool;
  auto CheckCoherenceReadback(CoherenceReadbackSlot& slot) -> void;
  OXGN_RNDR_API auto EnsureViewResolveResources(ViewId view_id,
    std::uint32_t required_entry_count) -> ViewResolveResources*;
  OXGN_RNDR_API auto StageResolveStateUpload(
    ViewId view_id, const ViewCacheEntry& state) -> void;
};

} // namespace oxygen::renderer::internal
