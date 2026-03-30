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

#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/Types/RasterShadowRenderPlan.h>
#include <Oxygen/Renderer/Types/ShadowFramePublication.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
class StagingProvider;
}

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
}

namespace oxygen::scene {
class Scene;
}

namespace oxygen::renderer {
namespace internal {
  class ConventionalShadowBackend;
}
namespace vsm {
  class VsmShadowRenderer;
}

//! Renderer-owned shadow-product scheduler and backend coordinator.
class ShadowManager {
public:
  using ProviderT = engine::upload::StagingProvider;
  using CoordinatorT = engine::upload::InlineTransfersCoordinator;

  OXGN_RNDR_API ShadowManager(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> provider,
    observer_ptr<CoordinatorT> inline_transfers,
    oxygen::ShadowQualityTier quality_tier = oxygen::ShadowQualityTier::kHigh,
    oxygen::DirectionalShadowImplementationPolicy directional_policy
    = oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly);

  OXYGEN_MAKE_NON_COPYABLE(ShadowManager)
  OXYGEN_MAKE_NON_MOVABLE(ShadowManager)

  OXGN_RNDR_API ~ShadowManager();

  OXGN_RNDR_API auto OnFrameStart(
    RendererTag tag, frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_RNDR_API auto ResetCachedState() -> void;

  OXGN_RNDR_API auto PublishForView(ViewId view_id,
    const engine::ViewConstants& view_constants, const LightManager& lights,
    observer_ptr<scene::Scene> active_scene, float camera_viewport_width,
    std::span<const engine::sceneprep::RenderItemData> rendered_items = {},
    std::span<const glm::vec4> shadow_caster_bounds = {},
    std::span<const glm::vec4> visible_receiver_bounds = {},
    std::chrono::milliseconds gpu_budget = std::chrono::milliseconds(16),
    std::uint64_t shadow_caster_content_hash = 0U) -> ShadowFramePublication;
  OXGN_RNDR_API auto SetPublishedViewFrameBindingsSlot(
    ViewId view_id, engine::BindlessViewFrameBindingsSlot slot) -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetFramePublication(
    ViewId view_id) const noexcept -> const ShadowFramePublication*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetRasterRenderPlan(
    ViewId view_id) const noexcept -> const RasterShadowRenderPlan*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetShadowInstanceMetadata(
    ViewId view_id) const noexcept -> const engine::ShadowInstanceMetadata*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetConventionalShadowDepthTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetVirtualShadowRenderer() const noexcept
    -> observer_ptr<vsm::VsmShadowRenderer>;

private:
  struct VirtualViewCacheEntry {
    std::vector<engine::ShadowInstanceMetadata> shadow_instances {};
    ShadowFramePublication frame_publication {};
  };

  using BufferT = engine::upload::TransientStructuredBuffer;

  observer_ptr<Graphics> gfx_;
  observer_ptr<ProviderT> staging_provider_;
  observer_ptr<CoordinatorT> inline_transfers_;
  oxygen::ShadowQualityTier shadow_quality_tier_ {
    oxygen::ShadowQualityTier::kHigh
  };
  oxygen::DirectionalShadowImplementationPolicy directional_policy_ {
    oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly
  };
  std::unordered_map<std::uint64_t, engine::ShadowImplementationKind>
    last_view_directional_implementation_;
  BufferT vsm_shadow_instance_buffer_;
  std::unordered_map<ViewId, VirtualViewCacheEntry> virtual_view_cache_;

  std::unique_ptr<internal::ConventionalShadowBackend> conventional_backend_;
  std::unique_ptr<vsm::VsmShadowRenderer> vsm_shadow_renderer_;
};

} // namespace oxygen::renderer
