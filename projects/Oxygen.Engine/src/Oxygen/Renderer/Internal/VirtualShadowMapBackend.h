//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

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
#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
class StagingProvider;
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
    std::chrono::milliseconds gpu_budget, bool allow_budget_fallback = true)
    -> ShadowFramePublication;
  OXGN_RNDR_API auto MarkRendered(ViewId view_id) -> void;
  OXGN_RNDR_API auto SetPublishedViewFrameBindingsSlot(
    ViewId view_id, engine::BindlessViewFrameBindingsSlot slot) -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetFramePublication(
    ViewId view_id) const noexcept -> const ShadowFramePublication*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetRenderPlan(
    ViewId view_id) const noexcept -> const VirtualShadowRenderPlan*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetViewIntrospection(
    ViewId view_id) const noexcept -> const VirtualShadowViewIntrospection*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPhysicalPoolTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;

private:
  struct PhysicalPoolConfig {
    std::uint32_t page_size_texels { 0U };
    std::uint32_t pages_per_clip_axis { 0U };
    std::uint32_t clip_level_count { 0U };
    std::uint32_t total_pages { 0U };
    std::uint32_t atlas_tiles_per_axis { 0U };
    std::uint32_t atlas_resolution { 0U };
  };

  struct PublicationKey {
    std::uint64_t view_hash { 0U };
    std::uint64_t candidate_hash { 0U };
    std::uint64_t caster_hash { 0U };
    std::uint64_t receiver_hash { 0U };

    [[nodiscard]] auto operator==(const PublicationKey&) const noexcept -> bool
      = default;
  };

  struct PhysicalTileAddress {
    std::uint16_t tile_x { 0U };
    std::uint16_t tile_y { 0U };
  };

  struct ResidentVirtualPage {
    PhysicalTileAddress tile {};
    bool contents_valid { false };
  };

  struct ViewCacheEntry {
    PublicationKey key {};
    std::vector<engine::ShadowInstanceMetadata> shadow_instances;
    std::vector<engine::DirectionalVirtualShadowMetadata>
      directional_virtual_metadata;
    std::vector<std::uint32_t> page_table_entries;
    std::vector<VirtualShadowRasterJob> raster_jobs;
    std::vector<VirtualShadowRasterJob> pending_raster_jobs;
    std::unordered_map<std::uint32_t, ResidentVirtualPage> resident_pages;
    ShadowFramePublication frame_publication {};
    VirtualShadowRenderPlan render_plan {};
    VirtualShadowViewIntrospection introspection {};
  };

  ::oxygen::Graphics* gfx_ { nullptr };
  ::oxygen::engine::upload::StagingProvider* staging_provider_ { nullptr };
  ::oxygen::engine::upload::InlineTransfersCoordinator* inline_transfers_ {
    nullptr
  };
  ::oxygen::ShadowQualityTier shadow_quality_tier_ {
    ::oxygen::ShadowQualityTier::kHigh
  };

  using BufferT = engine::upload::TransientStructuredBuffer;
  BufferT shadow_instance_buffer_;
  BufferT directional_virtual_metadata_buffer_;
  BufferT virtual_page_table_buffer_;

  std::shared_ptr<graphics::Texture> physical_pool_texture_;
  graphics::NativeView physical_pool_srv_view_ {};
  ShaderVisibleIndex physical_pool_srv_ { kInvalidShaderVisibleIndex };
  PhysicalPoolConfig physical_pool_config_ {};
  std::vector<PhysicalTileAddress> free_physical_tiles_;

  std::unordered_map<ViewId, ViewCacheEntry> view_cache_;

  OXGN_RNDR_API auto BuildPublicationKey(
    const engine::ViewConstants& view_constants,
    std::span<const engine::DirectionalShadowCandidate> directional_candidates,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds) const -> PublicationKey;
  OXGN_RNDR_API auto RefreshViewExports(ViewCacheEntry& state) const -> void;
  [[nodiscard]] OXGN_RNDR_NDAPI auto CanReuseResidentPages(
    const ViewCacheEntry& previous,
    const ViewCacheEntry& current) const noexcept -> bool;
  OXGN_RNDR_API auto BuildPhysicalPoolConfig(
    const engine::DirectionalShadowCandidate& candidate,
    std::chrono::milliseconds gpu_budget) const -> PhysicalPoolConfig;
  OXGN_RNDR_API auto EnsurePhysicalPool(const PhysicalPoolConfig& config)
    -> void;
  OXGN_RNDR_API auto ReleasePhysicalPool() -> void;
  OXGN_RNDR_API auto AllocatePhysicalTile()
    -> std::optional<PhysicalTileAddress>;
  OXGN_RNDR_API auto ReleasePhysicalTile(PhysicalTileAddress tile) -> void;
  OXGN_RNDR_API auto BuildDirectionalVirtualViewState(ViewId view_id,
    const engine::ViewConstants& view_constants,
    const engine::DirectionalShadowCandidate& candidate,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    std::uint32_t max_rendered_pages, const ViewCacheEntry* previous_state,
    ViewCacheEntry& state) -> void;

  OXGN_RNDR_API auto PublishShadowInstances(
    std::span<const engine::ShadowInstanceMetadata> instances)
    -> ShaderVisibleIndex;
  OXGN_RNDR_API auto PublishDirectionalVirtualMetadata(
    std::span<const engine::DirectionalVirtualShadowMetadata> metadata)
    -> ShaderVisibleIndex;
  OXGN_RNDR_API auto PublishPageTable(std::span<const std::uint32_t> entries)
    -> ShaderVisibleIndex;
};

} // namespace oxygen::renderer::internal
