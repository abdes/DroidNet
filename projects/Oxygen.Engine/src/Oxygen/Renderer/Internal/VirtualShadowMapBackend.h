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
#include <Oxygen/Renderer/Types/DirectionalVirtualBiasSettings.h>
#include <Oxygen/Renderer/Types/ShadowFramePublication.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/VirtualShadowCacheFrameData.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageFlags.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageTableEntry.h>
#include <Oxygen/Renderer/Types/VirtualShadowPhysicalPageMetadata.h>
#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>
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
  enum class CacheLifecycleState : std::uint8_t {
    kUninitialized = 0U,
    kResetPending = 1U,
    kClearing = 2U,
    kCleared = 3U,
    kWarmupRasterPending = 4U,
    kWarmupFeedbackPending = 5U,
    kValid = 6U,
    kDirty = 7U,
    kInvalidated = 8U,
    kRetired = 9U,
  };

  OXGN_RNDR_API VirtualShadowMapBackend(::oxygen::Graphics* gfx,
    ::oxygen::engine::upload::StagingProvider* provider,
    ::oxygen::engine::upload::InlineTransfersCoordinator* inline_transfers,
    ::oxygen::ShadowQualityTier quality_tier);

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowMapBackend)
  OXYGEN_MAKE_NON_MOVABLE(VirtualShadowMapBackend)

  OXGN_RNDR_API ~VirtualShadowMapBackend();

  OXGN_RNDR_API auto OnFrameStart(
    RendererTag tag, frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_RNDR_API auto ResetCachedState() -> void;
  OXGN_RNDR_API auto RetireView(ViewId view_id) -> void;

  OXGN_RNDR_API auto PublishView(ViewId view_id,
    const engine::ViewConstants& view_constants, float camera_viewport_width,
    std::span<const engine::DirectionalShadowCandidate> directional_candidates,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    std::chrono::milliseconds gpu_budget, std::uint64_t view_generation,
    std::uint64_t shadow_caster_content_hash = 0U) -> ShadowFramePublication;
  OXGN_RNDR_API auto ResolveCurrentFrame(ViewId view_id) -> void;
  OXGN_RNDR_API auto ApplyExtractedScheduleResult(ViewId view_id,
    frame::SequenceNumber source_sequence, std::uint64_t cache_epoch,
    std::uint64_t view_generation, std::uint32_t scheduled_page_count) -> void;
  OXGN_RNDR_API auto ApplyExtractedResolveStatsResult(ViewId view_id,
    frame::SequenceNumber source_sequence,
    std::uint64_t cache_epoch, std::uint64_t view_generation,
    const renderer::VirtualShadowResolveStats& resolve_stats) -> void;
  OXGN_RNDR_API auto PreparePageTableResources(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto PreparePageManagementOutputsForGpuWrite(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto FinalizePageManagementOutputs(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto SetPublishedViewFrameBindingsSlot(
    ViewId view_id, engine::BindlessViewFrameBindingsSlot slot) -> void;
  OXGN_RNDR_API auto SetDirectionalBiasSettings(
    const renderer::DirectionalVirtualBiasSettings& settings) noexcept -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetFramePublication(
    ViewId view_id) const noexcept -> const ShadowFramePublication*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetPageManagementBindings(
    ViewId view_id) const noexcept
    -> const renderer::VirtualShadowPageManagementBindings*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetPageManagementStateSnapshot(
    ViewId view_id) const noexcept
    -> std::optional<renderer::VirtualShadowPageManagementStateSnapshot>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetDirectionalVirtualMetadata(
    ViewId view_id) const noexcept
    -> const engine::DirectionalVirtualShadowMetadata*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetShadowInstanceMetadata(
    ViewId view_id) const noexcept -> const engine::ShadowInstanceMetadata*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetPageFlagsBuffer(
    ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetPhysicalPageMetadataBuffer(
    ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetResolveStatsBuffer(
    ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>;
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

  struct DirectionalVirtualClipmapSetup {
    bool valid { false };
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
  };

  struct ViewCacheEntry {
    struct ExtractedFeedbackDiagnostics {
      frame::SequenceNumber scheduled_source_sequence { 0U };
      frame::SequenceNumber resolve_stats_source_sequence { 0U };
      std::uint32_t scheduled_page_count { 0U };
      renderer::VirtualShadowResolveStats resolve_stats {};
      bool has_schedule_feedback { false };
      bool has_resolve_stats_feedback { false };
    };

    struct PendingResidencyResolve {
      bool valid { false };
      // This is the freshness latch for the live export path. A pending
      // resolve packet can exist in cache state without being safe to push
      // back into the authoritative page-management bindings.
      bool has_fresh_pending_resolve_inputs { false };
      bool reset_page_management_state { false };
      engine::ViewConstants view_constants {};
      glm::mat4 previous_light_view { 1.0F };
      bool global_dirty_resident_contents { false };
      ShaderVisibleIndex previous_shadow_caster_bounds_srv {
        kInvalidShaderVisibleIndex
      };
      ShaderVisibleIndex current_shadow_caster_bounds_srv {
        kInvalidShaderVisibleIndex
      };
      std::uint32_t shadow_caster_bound_count { 0U };
    };

    std::vector<engine::ShadowInstanceMetadata> shadow_instances;
    std::vector<engine::DirectionalVirtualShadowMetadata>
      directional_virtual_metadata;
    std::vector<glm::vec4> shadow_caster_bounds;
    std::uint64_t shadow_caster_content_hash { 0U };
    renderer::VirtualShadowCacheFrameData prev_frame {};
    renderer::VirtualShadowCacheFrameData current_frame {};
    ExtractedFeedbackDiagnostics extracted_feedback {};
    PendingResidencyResolve pending_residency_resolve {};
    ShadowFramePublication frame_publication {};
    renderer::VirtualShadowPageManagementBindings page_management_bindings {};
    CacheLifecycleState lifecycle_state { CacheLifecycleState::kUninitialized };
    std::uint64_t cache_epoch { 0U };
    std::uint64_t view_generation { 0U };
    bool bootstrap_feedback_complete { false };
    bool stable_validation_pending { false };
    bool preserve_rendered_basis_until_next_raster { false };
    bool current_frame_requested_reset { false };
  };

  struct DirectionalCacheReuseAssessment {
    const engine::DirectionalVirtualShadowMetadata* previous_metadata {
      nullptr
    };
    const std::vector<glm::vec4>* previous_shadow_caster_bounds { nullptr };
    std::int32_t first_content_mismatch_clip_index { -1 };
    bool compare_shadow_caster_bounds_on_gpu { false };
    bool reset_page_management_state { true };
    bool global_dirty_resident_contents { false };
    bool invalidate_rendered_cache_history { true };
    bool current_view_is_uncached { true };
    bool has_reusable_rendered_cache { false };
    bool clipmap_key_matches { false };
    bool address_space_compatible { false };
    bool content_compatible { false };
    bool shadow_content_changed { false };
    bool shadow_bounds_count_match { false };
  };

  ::oxygen::Graphics* gfx_ { nullptr };
  ::oxygen::engine::upload::StagingProvider* staging_provider_ { nullptr };
  ::oxygen::engine::upload::InlineTransfersCoordinator* inline_transfers_ {
    nullptr
  };
  ::oxygen::ShadowQualityTier shadow_quality_tier_ {
    ::oxygen::ShadowQualityTier::kHigh
  };
  renderer::DirectionalVirtualBiasSettings directional_bias_settings_ {};
  frame::SequenceNumber frame_sequence_ { 0U };
  frame::Slot frame_slot_ { frame::kInvalidSlot };

  using BufferT = engine::upload::TransientStructuredBuffer;
  BufferT shadow_instance_buffer_;
  BufferT directional_virtual_metadata_buffer_;
  BufferT shadow_caster_bounds_buffer_;

  struct ViewStructuredWordBufferResources {
    std::shared_ptr<graphics::Buffer> gpu_buffer;
    std::shared_ptr<graphics::Buffer> upload_buffer;
    std::uint32_t* mapped_upload { nullptr };
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav { kInvalidShaderVisibleIndex };
    std::uint32_t entry_capacity { 0U };
    std::uint64_t cache_epoch { 0U };
    std::uint64_t view_generation { 0U };
  };

  struct ViewResolveResources {
    std::shared_ptr<graphics::Buffer> stats_gpu_buffer;
    ShaderVisibleIndex stats_uav { kInvalidShaderVisibleIndex };

    std::shared_ptr<graphics::Buffer> dirty_page_flags_gpu_buffer;
    ShaderVisibleIndex dirty_page_flags_uav { kInvalidShaderVisibleIndex };
    std::uint32_t dirty_page_flags_capacity { 0U };

    std::shared_ptr<graphics::Buffer> physical_page_metadata_gpu_buffer;
    ShaderVisibleIndex physical_page_metadata_srv {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex physical_page_metadata_uav {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t physical_page_metadata_capacity { 0U };

    std::shared_ptr<graphics::Buffer> physical_page_lists_gpu_buffer;
    ShaderVisibleIndex physical_page_lists_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex physical_page_lists_uav { kInvalidShaderVisibleIndex };
    std::uint32_t physical_page_lists_capacity { 0U };
    bool physical_page_state_reset_pending { true };
    std::uint64_t cache_epoch { 0U };
    std::uint64_t view_generation { 0U };
  };

  std::shared_ptr<graphics::Texture> physical_pool_texture_;
  graphics::NativeView physical_pool_srv_view_ {};
  ShaderVisibleIndex physical_pool_srv_ { kInvalidShaderVisibleIndex };
  PhysicalPoolConfig physical_pool_config_ {};
  std::uint64_t cache_epoch_ { 1U };
  std::unordered_map<ViewId, std::uint64_t> view_generations_;

  std::unordered_map<ViewId, ViewCacheEntry> view_cache_;
  std::unordered_map<ViewId, ViewStructuredWordBufferResources>
    view_page_management_page_table_resources_;
  std::unordered_map<ViewId, ViewStructuredWordBufferResources>
    view_page_management_page_flags_resources_;
  std::unordered_map<ViewId, ViewResolveResources> view_resolve_resources_;
  OXGN_RNDR_API auto InitializeDirectionalViewStateFromClipmapSetup(
    const DirectionalVirtualClipmapSetup& setup,
    std::span<const glm::vec4> shadow_caster_bounds,
    ViewCacheEntry& state) const -> void;
  OXGN_RNDR_API static auto RollExtractedCacheFrames(
    ViewCacheEntry& state) noexcept -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto AssessDirectionalCacheReuse(
    const DirectionalVirtualClipmapSetup& setup,
    const ViewCacheEntry* previous_state, const ViewCacheEntry& state) const
    -> DirectionalCacheReuseAssessment;
  [[nodiscard]] OXGN_RNDR_NDAPI auto HasReusableRenderedCache(
    const ViewCacheEntry* previous_state) const noexcept -> bool;
  OXGN_RNDR_API auto RollForwardCacheHistory(
    const ViewCacheEntry* previous_state, ViewCacheEntry& state) const noexcept
    -> void;
  OXGN_RNDR_API static auto InvalidateRenderedCacheHistory(
    ViewCacheEntry& state) noexcept -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI static auto IsPublicationLiveState(
    CacheLifecycleState state) noexcept -> bool;
  [[nodiscard]] OXGN_RNDR_NDAPI static auto CacheLifecycleStateName(
    CacheLifecycleState state) noexcept -> const char*;
  OXGN_RNDR_API auto SetLifecycleState(ViewId view_id, ViewCacheEntry& state,
    CacheLifecycleState next_state, const char* reason) const -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetOrCreateViewGeneration(ViewId view_id)
    -> std::uint64_t;
  auto SyncViewGeneration(ViewId view_id, std::uint64_t view_generation)
    -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto ResourceOwnershipMatches(
    const ViewStructuredWordBufferResources& resources,
    const ViewCacheEntry& state) const noexcept -> bool;
  [[nodiscard]] OXGN_RNDR_NDAPI auto ResourceOwnershipMatches(
    const ViewResolveResources& resources,
    const ViewCacheEntry& state) const noexcept -> bool;
  [[nodiscard]] OXGN_RNDR_NDAPI static auto
  CanApplyPendingResolveToLiveBindings(const ViewCacheEntry& state) noexcept
    -> bool;
  [[nodiscard]] OXGN_RNDR_NDAPI auto IsDirectionalViewCacheCompatible(
    const DirectionalVirtualClipmapSetup& setup,
    const ViewCacheEntry* previous_state) const -> bool;
  OXGN_RNDR_API auto PopulateDirectionalPendingResolve(
    const DirectionalCacheReuseAssessment& assessment,
    const engine::ViewConstants& view_constants, ViewCacheEntry& state) -> void;
  OXGN_RNDR_API auto RefreshViewExports(
    ViewId view_id, ViewCacheEntry& state) const -> void;
  OXGN_RNDR_API auto BuildPhysicalPoolConfig(
    const engine::DirectionalShadowCandidate& candidate,
    std::chrono::milliseconds gpu_budget, std::size_t shadow_caster_count) const
    -> PhysicalPoolConfig;
  OXGN_RNDR_API auto EnsurePhysicalPool(const PhysicalPoolConfig& config)
    -> void;
  OXGN_RNDR_API auto ReleasePhysicalPool() -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto PrepareDirectionalVirtualClipmapSetup(
    const engine::ViewConstants& view_constants, float camera_viewport_width,
    const engine::DirectionalShadowCandidate& candidate,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    const ViewCacheEntry* previous_state) const
    -> std::optional<DirectionalVirtualClipmapSetup>;
  OXGN_RNDR_API auto BuildDirectionalVirtualViewState(ViewId view_id,
    const engine::ViewConstants& view_constants, float camera_viewport_width,
    const engine::DirectionalShadowCandidate& candidate,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    const ViewCacheEntry* previous_state, ViewCacheEntry& state) -> void;
  OXGN_RNDR_API auto PublishShadowInstances(
    std::span<const engine::ShadowInstanceMetadata> instances)
    -> ShaderVisibleIndex;
  OXGN_RNDR_API auto PublishDirectionalVirtualMetadata(
    std::span<const engine::DirectionalVirtualShadowMetadata> metadata)
    -> ShaderVisibleIndex;
  OXGN_RNDR_API auto EnsureViewPageManagementPageTableResources(ViewId view_id,
    std::uint32_t required_entry_count) -> ViewStructuredWordBufferResources*;
  OXGN_RNDR_API auto EnsureViewPageManagementPageFlagResources(ViewId view_id,
    std::uint32_t required_entry_count) -> ViewStructuredWordBufferResources*;
  OXGN_RNDR_API auto EnsureViewResolveResources(ViewId view_id)
    -> ViewResolveResources*;
};

} // namespace oxygen::renderer::internal
