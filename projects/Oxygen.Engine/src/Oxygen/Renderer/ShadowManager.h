//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Types/DirectionalShadowMetadata.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
class StagingProvider;
}

namespace oxygen::renderer {

//! Renderer-owned shadow-product scheduler and metadata publisher.
/*!
 Phase 1 starts with directional conventional shadow products only. The class
 owns shadow-product publication so LightManager remains a light collector,
 not a shadow runtime.
*/
class ShadowManager {
public:
  struct SyntheticSunShadowInput {
    bool enabled { false };
    glm::vec3 direction_ws { 0.0F, 0.0F, -1.0F };
    float bias { 0.0F };
    float normal_bias { 0.0F };
    std::uint32_t resolution_hint { static_cast<std::uint32_t>(
      scene::ShadowResolutionHint::kMedium) };
    std::uint32_t cascade_count { scene::kMaxShadowCascades };
    float distribution_exponent { 1.0F };
    std::array<float, scene::kMaxShadowCascades> cascade_distances {};
  };

  struct PublishedViewData {
    ShaderVisibleIndex shadow_instance_metadata_srv {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex directional_shadow_metadata_srv {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex directional_shadow_texture_srv {
      kInvalidShaderVisibleIndex
    };

    std::span<const engine::ShadowInstanceMetadata> shadow_instances {};
    std::span<const engine::DirectionalShadowMetadata> directional_metadata {};
    std::span<const engine::ViewConstants::GpuData>
      directional_view_constants {};
    std::uint32_t sun_shadow_index { 0xFFFFFFFFU };
  };

  using ProviderT = engine::upload::StagingProvider;
  using CoordinatorT = engine::upload::InlineTransfersCoordinator;

  OXGN_RNDR_API ShadowManager(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> provider,
    observer_ptr<CoordinatorT> inline_transfers,
    oxygen::ShadowQualityTier quality_tier = oxygen::ShadowQualityTier::kHigh);

  OXYGEN_MAKE_NON_COPYABLE(ShadowManager)
  OXYGEN_MAKE_NON_MOVABLE(ShadowManager)

  OXGN_RNDR_API ~ShadowManager();

  OXGN_RNDR_API auto OnFrameStart(
    RendererTag tag, frame::SequenceNumber sequence, frame::Slot slot) -> void;

  OXGN_RNDR_API auto PublishForView(ViewId view_id,
    const engine::ViewConstants& view_constants, const LightManager& lights,
    std::span<const glm::vec4> shadow_caster_bounds = {},
    const SyntheticSunShadowInput* synthetic_sun_shadow = nullptr)
    -> PublishedViewData;
  OXGN_RNDR_API auto SetPublishedViewFrameBindingsSlot(
    ViewId view_id, engine::BindlessViewFrameBindingsSlot slot) -> void;

  OXGN_RNDR_NDAPI auto TryGetPublishedViewData(ViewId view_id) const noexcept
    -> const PublishedViewData*;
  OXGN_RNDR_NDAPI auto GetDirectionalShadowTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;

private:
  struct DirectionalShadowResourceConfig {
    std::uint32_t resolution { 0U };
    std::uint32_t required_layers { 0U };
  };

  struct PublishedViewState {
    std::vector<engine::ShadowInstanceMetadata> shadow_instances;
    std::vector<engine::DirectionalShadowMetadata> directional_metadata;
    std::vector<engine::ViewConstants::GpuData> directional_view_constants;
    PublishedViewData published {};
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

  std::unordered_map<ViewId, PublishedViewState> published_views_;

  OXGN_RNDR_API auto BuildDirectionalResourceConfig(
    std::span<const engine::DirectionalShadowCandidate> candidates) const
    -> DirectionalShadowResourceConfig;
  OXGN_RNDR_API auto EnsureDirectionalResources(
    const DirectionalShadowResourceConfig& config) -> void;
  OXGN_RNDR_API auto ReleaseDirectionalResources() -> void;
  OXGN_RNDR_API auto BuildDirectionalViewState(ViewId view_id,
    const engine::ViewConstants& view_constants,
    std::span<const engine::DirectionalShadowCandidate> candidates,
    std::span<const glm::vec4> shadow_caster_bounds, PublishedViewState& state)
    -> void;

  OXGN_RNDR_API auto PublishShadowInstances(
    std::span<const engine::ShadowInstanceMetadata> instances)
    -> ShaderVisibleIndex;
  OXGN_RNDR_API auto PublishDirectionalMetadata(
    std::span<const engine::DirectionalShadowMetadata> metadata)
    -> ShaderVisibleIndex;
};

} // namespace oxygen::renderer
