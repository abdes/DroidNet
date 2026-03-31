//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Types/DirectionalShadowCandidate.h>
#include <Oxygen/Renderer/Types/RasterShadowRenderPlan.h>
#include <Oxygen/Renderer/Types/ShadowFramePublication.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
class StagingProvider;
}

namespace oxygen::renderer::internal {

//! Conventional raster-shadow runtime for pooled depth-map products.
class ConventionalShadowBackend {
public:
  using ProviderT = engine::upload::StagingProvider;
  using CoordinatorT = engine::upload::InlineTransfersCoordinator;

  OXGN_RNDR_API ConventionalShadowBackend(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> provider,
    observer_ptr<CoordinatorT> inline_transfers,
    oxygen::ShadowQualityTier quality_tier);

  OXYGEN_MAKE_NON_COPYABLE(ConventionalShadowBackend)
  OXYGEN_MAKE_NON_MOVABLE(ConventionalShadowBackend)

  OXGN_RNDR_API ~ConventionalShadowBackend();

  OXGN_RNDR_API auto OnFrameStart(
    RendererTag tag, frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_RNDR_API auto ResetCachedState() -> void;
  OXGN_RNDR_API auto ReserveFrameResources(
    std::span<const engine::DirectionalShadowCandidate> directional_candidates,
    std::uint32_t scene_view_count) -> void;

  OXGN_RNDR_API auto PublishView(ViewId view_id,
    const engine::ViewConstants& view_constants,
    std::span<const engine::DirectionalShadowCandidate> directional_candidates,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds = {},
    std::uint64_t shadow_caster_content_hash = 0U) -> ShadowFramePublication;
  OXGN_RNDR_API auto SetPublishedViewFrameBindingsSlot(
    ViewId view_id, engine::BindlessViewFrameBindingsSlot slot) -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetFramePublication(
    ViewId view_id) const noexcept -> const ShadowFramePublication*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetShadowInstanceMetadata(
    ViewId view_id) const noexcept -> const engine::ShadowInstanceMetadata*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetRasterRenderPlan(
    ViewId view_id) const noexcept -> const RasterShadowRenderPlan*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetDirectionalShadowTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;

private:
  struct DirectionalShadowResourceConfig {
    std::uint32_t resolution { 0U };
    std::uint32_t required_layers { 0U };
  };

  struct PublicationKey {
    std::uint64_t view_hash { 0U };
    std::uint64_t candidate_hash { 0U };
    std::uint64_t caster_hash { 0U };
    std::uint64_t receiver_hash { 0U };
    std::uint64_t shadow_content_hash { 0U };

    [[nodiscard]] auto operator==(const PublicationKey&) const noexcept -> bool
      = default;
  };

  struct ViewCacheEntry {
    PublicationKey key {};
    std::uint32_t directional_layers_used { 0U };
    std::uint32_t required_directional_resolution { 0U };
    std::vector<engine::ShadowInstanceMetadata> shadow_instances;
    std::vector<engine::DirectionalShadowMetadata> directional_metadata;
    std::vector<RasterShadowJob> raster_jobs;
    ShadowFramePublication frame_publication {};
    RasterShadowRenderPlan raster_plan {};
  };

  observer_ptr<Graphics> gfx_;
  observer_ptr<ProviderT> staging_provider_;
  observer_ptr<CoordinatorT> inline_transfers_;
  oxygen::ShadowQualityTier shadow_quality_tier_ {
    oxygen::ShadowQualityTier::kHigh
  };

  using BufferT = engine::upload::TransientStructuredBuffer;
  BufferT shadow_instance_buffer_;
  BufferT directional_shadow_metadata_buffer_;

  std::shared_ptr<graphics::Texture> directional_shadow_texture_;
  graphics::NativeView directional_shadow_texture_srv_view_ {};
  ShaderVisibleIndex directional_shadow_texture_srv_ {
    kInvalidShaderVisibleIndex
  };
  std::uint32_t directional_shadow_resolution_ { 0U };
  std::uint32_t directional_shadow_capacity_layers_ { 0U };

  std::unordered_map<ViewId, ViewCacheEntry> view_cache_;

  OXGN_RNDR_API auto BuildPublicationKey(
    const engine::ViewConstants& view_constants,
    std::span<const engine::DirectionalShadowCandidate> directional_candidates,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    std::uint64_t shadow_caster_content_hash) const -> PublicationKey;
  [[nodiscard]] OXGN_RNDR_NDAPI auto CountDirectionalLayers(
    std::span<const engine::DirectionalShadowCandidate> candidates) const
    -> std::uint32_t;
  [[nodiscard]] OXGN_RNDR_NDAPI auto CountPublishedDirectionalLayers(
    std::optional<ViewId> excluded_view_id = {}) const -> std::uint32_t;
  [[nodiscard]] OXGN_RNDR_NDAPI auto MaxPublishedDirectionalResolution(
    std::optional<ViewId> excluded_view_id = {}) const -> std::uint32_t;
  OXGN_RNDR_API auto RefreshViewExports(ViewCacheEntry& state) const -> void;
  OXGN_RNDR_API auto RefreshAllViewExports() -> void;
  OXGN_RNDR_API auto BuildDirectionalResourceConfig(
    std::span<const engine::DirectionalShadowCandidate> candidates,
    std::uint32_t required_layers, std::uint32_t required_resolution) const
    -> DirectionalShadowResourceConfig;
  OXGN_RNDR_API auto EnsureDirectionalResources(
    const DirectionalShadowResourceConfig& config) -> void;
  OXGN_RNDR_API auto ReleaseDirectionalResources() -> void;
  OXGN_RNDR_API auto BuildDirectionalViewState(ViewId view_id,
    const engine::ViewConstants& view_constants,
    std::span<const engine::DirectionalShadowCandidate> candidates,
    std::span<const glm::vec4> shadow_caster_bounds,
    std::span<const glm::vec4> visible_receiver_bounds,
    std::uint32_t base_resource_layer, ViewCacheEntry& state) -> void;

  OXGN_RNDR_API auto PublishShadowInstances(
    std::span<const engine::ShadowInstanceMetadata> instances)
    -> ShaderVisibleIndex;
  OXGN_RNDR_API auto PublishDirectionalMetadata(
    std::span<const engine::DirectionalShadowMetadata> metadata)
    -> ShaderVisibleIndex;
};

} // namespace oxygen::renderer::internal
