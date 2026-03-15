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
#include <Oxygen/Renderer/Types/ShadowFramePublication.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
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
    std::uint64_t shadow_caster_content_hash = 0U)
    -> ShadowFramePublication;
  OXGN_RNDR_API auto ResolveCurrentFrame(ViewId view_id) -> void;
  OXGN_RNDR_API auto MarkRendered(
    ViewId view_id, bool rendered_page_work) -> void;
  OXGN_RNDR_API auto PreparePageTableResources(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto PreparePageManagementOutputsForGpuWrite(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto FinalizePageManagementOutputs(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto SetPublishedViewFrameBindingsSlot(
    ViewId view_id, engine::BindlessViewFrameBindingsSlot slot) -> void;
  OXGN_RNDR_API auto SetDirectionalCacheControls(
    renderer::DirectionalVirtualCacheControls controls) -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetDirectionalCacheControls() const noexcept
    -> renderer::DirectionalVirtualCacheControls;

  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetFramePublication(
    ViewId view_id) const noexcept -> const ShadowFramePublication*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetPageManagementBindings(
    ViewId view_id) const noexcept
    -> const renderer::VirtualShadowPageManagementBindings*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetDirectionalVirtualMetadata(
    ViewId view_id) const noexcept
    -> const engine::DirectionalVirtualShadowMetadata*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPhysicalPoolTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;

private:
  struct alignas(16) DirectionalInvalidationRect {
    std::int32_t min_grid_x { 0 };
    std::int32_t max_grid_x { 0 };
    std::int32_t min_grid_y { 0 };
    std::int32_t max_grid_y { 0 };
    std::uint32_t clip_index { 0U };
    std::uint32_t page_flags { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
  };

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
    struct PendingResidencyResolve {
      bool valid { false };
      bool dirty { false };
      bool reset_page_management_state { false };
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
      bool global_dirty_resident_contents { false };
      std::vector<DirectionalInvalidationRect> invalidation_rects {};
    };

    PublicationKey key {};
    std::vector<engine::ShadowInstanceMetadata> shadow_instances;
    std::vector<engine::DirectionalVirtualShadowMetadata>
      directional_virtual_metadata;
    std::vector<glm::vec4> shadow_caster_bounds;
    bool has_rendered_cache_history { false };
    PendingResidencyResolve pending_residency_resolve {};
    renderer::VirtualShadowResolveStats resolve_stats {};
    ShadowFramePublication frame_publication {};
    renderer::VirtualShadowPageManagementBindings page_management_bindings {};
  };

  struct DirectionalSelectionBuildResult {
    bool address_space_compatible { false };
    bool previous_page_management_state_exists { false };
  };

  struct DirectionalInvalidationBuildResult {
    std::vector<DirectionalInvalidationRect> invalidation_rects {};
    bool global_dirty_resident_contents { false };
  };

  struct DirectionalPreviousStateContext {
    const PublicationKey* previous_key { nullptr };
    const engine::DirectionalVirtualShadowMetadata* previous_metadata {
      nullptr
    };
    const std::vector<glm::vec4>* previous_shadow_caster_bounds { nullptr };
    bool rendered_cache_history_available { false };
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
    std::shared_ptr<graphics::Buffer> stats_gpu_buffer;
    ShaderVisibleIndex stats_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex stats_uav { kInvalidShaderVisibleIndex };

    std::shared_ptr<graphics::Buffer> dirty_page_flags_gpu_buffer;
    ShaderVisibleIndex dirty_page_flags_uav { kInvalidShaderVisibleIndex };
    std::uint32_t dirty_page_flags_capacity { 0U };

    std::shared_ptr<graphics::Buffer> invalidation_rects_gpu_buffer;
    std::shared_ptr<graphics::Buffer> invalidation_rects_upload_buffer;
    DirectionalInvalidationRect* mapped_invalidation_rects_upload { nullptr };
    ShaderVisibleIndex invalidation_rects_srv { kInvalidShaderVisibleIndex };
    std::uint32_t invalidation_rect_capacity { 0U };
    std::uint32_t invalidation_rect_count { 0U };
    bool invalidation_rects_upload_pending { false };

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
  };

  std::shared_ptr<graphics::Texture> physical_pool_texture_;
  graphics::NativeView physical_pool_srv_view_ {};
  ShaderVisibleIndex physical_pool_srv_ { kInvalidShaderVisibleIndex };
  PhysicalPoolConfig physical_pool_config_ {};
  std::vector<PhysicalTileAddress> free_physical_tiles_;
  renderer::DirectionalVirtualCacheControls directional_cache_controls_ {};

  std::unordered_map<ViewId, ViewCacheEntry> view_cache_;
  std::unordered_map<ViewId, ViewStructuredWordBufferResources>
    view_page_management_page_table_resources_;
  std::unordered_map<ViewId, ViewStructuredWordBufferResources>
    view_page_management_page_flags_resources_;
  std::unordered_map<ViewId, ViewResolveResources> view_resolve_resources_;
  OXGN_RNDR_API auto BuildPublicationKey(
    const engine::ViewConstants& view_constants,
    std::span<const engine::DirectionalShadowCandidate> directional_candidates,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::uint64_t shadow_caster_content_hash) const -> PublicationKey;
  OXGN_RNDR_API auto InitializeDirectionalViewStateFromClipmapSetup(
    const DirectionalVirtualClipmapSetup& setup,
    std::span<const glm::vec4> shadow_caster_bounds,
    ViewCacheEntry& state) const -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto BuildDirectionalPreviousStateContext(
    const ViewCacheEntry* previous_state) const
    -> DirectionalPreviousStateContext;
  [[nodiscard]] OXGN_RNDR_NDAPI auto BuildDirectionalInvalidationResult(
    const DirectionalVirtualClipmapSetup& setup,
    const PublicationKey* previous_key, const PublicationKey& current_key,
    const engine::DirectionalVirtualShadowMetadata* previous_metadata,
    const std::vector<glm::vec4>* previous_shadow_caster_bounds,
    std::span<const glm::vec4> shadow_caster_bounds,
    bool address_space_compatible) const -> DirectionalInvalidationBuildResult;
  [[nodiscard]] OXGN_RNDR_NDAPI auto BuildDirectionalSelectionResult(
    ViewId view_id, const DirectionalVirtualClipmapSetup& setup,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    const engine::DirectionalVirtualShadowMetadata* previous_metadata,
    const ViewCacheEntry* previous_state, ViewCacheEntry& state) const
    -> DirectionalSelectionBuildResult;
  OXGN_RNDR_API auto PopulateDirectionalPendingResolve(
    ViewCacheEntry& state, const DirectionalVirtualClipmapSetup& setup,
    DirectionalSelectionBuildResult selection,
    DirectionalInvalidationBuildResult invalidation,
    const engine::ViewConstants& view_constants,
    std::uint32_t visible_receiver_bound_count)
    const -> void;
  OXGN_RNDR_API auto BuildDirectionalPendingResolveStage(
    ViewId view_id, const DirectionalVirtualClipmapSetup& setup,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    const DirectionalPreviousStateContext& previous_context,
    const ViewCacheEntry* previous_state,
    const engine::ViewConstants& view_constants, ViewCacheEntry& state) const
    -> void;
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
  OXGN_RNDR_API auto StagePageManagementSeedUpload(
    ViewId view_id, ViewCacheEntry& state) -> void;
  OXGN_RNDR_API auto StageInvalidationUploads(
    ViewId view_id, ViewCacheEntry& state) -> void;
};

} // namespace oxygen::renderer::internal
