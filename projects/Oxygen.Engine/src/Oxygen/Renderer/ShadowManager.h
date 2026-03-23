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
#include <Oxygen/Renderer/Types/DirectionalVirtualBiasSettings.h>
#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>
#include <Oxygen/Renderer/Types/RasterShadowRenderPlan.h>
#include <Oxygen/Renderer/Types/ShadowFramePublication.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
class StagingProvider;
}

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
}

namespace oxygen::renderer {
namespace internal {
  class ConventionalShadowBackend;
  class VirtualShadowMapBackend;
}

//! Renderer-owned shadow-product scheduler and backend coordinator.
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
  OXGN_RNDR_API auto RetireView(ViewId view_id) -> void;

  OXGN_RNDR_API auto PublishForView(ViewId view_id,
    const engine::ViewConstants& view_constants, const LightManager& lights,
    float camera_viewport_width,
    std::span<const glm::vec4> shadow_caster_bounds = {},
    std::span<const glm::vec4> visible_receiver_bounds = {},
    const SyntheticSunShadowInput* synthetic_sun_shadow = nullptr,
    std::chrono::milliseconds gpu_budget = std::chrono::milliseconds(16),
    std::uint64_t shadow_caster_content_hash = 0U) -> ShadowFramePublication;
  OXGN_RNDR_API auto ResolveVirtualCurrentFrame(ViewId view_id) -> void;
  OXGN_RNDR_API auto PrepareVirtualPageTableResources(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto PrepareVirtualPageManagementOutputsForGpuWrite(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto FinalizeVirtualPageManagementOutputs(
    ViewId view_id, graphics::CommandRecorder& recorder) -> void;
  OXGN_RNDR_API auto SetPublishedViewFrameBindingsSlot(
    ViewId view_id, engine::BindlessViewFrameBindingsSlot slot) -> void;
  OXGN_RNDR_API auto SetDirectionalVirtualBiasSettings(
    const DirectionalVirtualBiasSettings& settings) noexcept -> void;
  OXGN_RNDR_API auto SubmitVirtualGpuRasterInputs(
    ViewId view_id, renderer::VirtualShadowGpuRasterInputs inputs) -> void;
  OXGN_RNDR_API auto ClearVirtualGpuRasterInputs(ViewId view_id) -> void;
  OXGN_RNDR_API auto RegisterVirtualScheduleExtraction(ViewId view_id,
    std::shared_ptr<graphics::Buffer> readback_buffer,
    std::uint32_t* mapped_readback_ptr, frame::SequenceNumber source_sequence,
    frame::Slot source_slot) -> void;
  OXGN_RNDR_API auto RegisterVirtualResolveStatsExtraction(ViewId view_id,
    std::shared_ptr<graphics::Buffer> readback_buffer,
    renderer::VirtualShadowResolveStats* mapped_readback_ptr,
    frame::SequenceNumber source_sequence, frame::Slot source_slot) -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetFramePublication(
    ViewId view_id) const noexcept -> const ShadowFramePublication*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetRasterRenderPlan(
    ViewId view_id) const noexcept -> const RasterShadowRenderPlan*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetVirtualGpuRasterInputs(
    ViewId view_id) const noexcept
    -> const renderer::VirtualShadowGpuRasterInputs*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetVirtualFramePacket(
    ViewId view_id) const noexcept -> const renderer::VirtualShadowFramePacket*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetVirtualPageManagementBindings(
    ViewId view_id) const noexcept
    -> const VirtualShadowPageManagementBindings*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetVirtualPageManagementStateSnapshot(
    ViewId view_id) const noexcept
    -> std::optional<VirtualShadowPageManagementStateSnapshot>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetVirtualDirectionalMetadata(
    ViewId view_id) const noexcept
    -> const engine::DirectionalVirtualShadowMetadata*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetShadowInstanceMetadata(
    ViewId view_id) const noexcept -> const engine::ShadowInstanceMetadata*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetVirtualPageFlagsBuffer(
    ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetVirtualPhysicalPageMetadataBuffer(
    ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetVirtualResolveStatsBuffer(
    ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetConventionalShadowDepthTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;
  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetVirtualShadowDepthTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;

private:
  struct VirtualShadowScheduleExtraction {
    std::shared_ptr<graphics::Buffer> readback_buffer {};
    std::uint32_t* mapped_readback_ptr { nullptr };
    frame::SequenceNumber source_frame_sequence { 0U };
    frame::SequenceNumber last_consumed_sequence { 0U };
    std::uint64_t cache_epoch { 0U };
    std::uint64_t view_generation { 0U };
    bool pending_readback { false };
  };

  struct VirtualShadowResolveStatsExtraction {
    std::shared_ptr<graphics::Buffer> readback_buffer {};
    renderer::VirtualShadowResolveStats* mapped_readback_ptr { nullptr };
    frame::SequenceNumber source_frame_sequence { 0U };
    frame::SequenceNumber last_consumed_sequence { 0U };
    std::uint64_t cache_epoch { 0U };
    std::uint64_t view_generation { 0U };
    bool pending_readback { false };
  };

  [[nodiscard]] auto GetOrCreateViewGeneration(ViewId view_id) -> std::uint64_t;
  [[nodiscard]] auto GetViewGeneration(ViewId view_id) const -> std::uint64_t;
  auto RefreshVirtualFramePacket(ViewId view_id) -> void;

  observer_ptr<Graphics> gfx_;
  observer_ptr<ProviderT> staging_provider_;
  observer_ptr<CoordinatorT> inline_transfers_;
  oxygen::ShadowQualityTier shadow_quality_tier_ {
    oxygen::ShadowQualityTier::kHigh
  };
  oxygen::DirectionalShadowImplementationPolicy directional_policy_ {
    oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly
  };
  DirectionalVirtualBiasSettings directional_virtual_bias_settings_ {};
  std::unordered_map<std::uint64_t, engine::ShadowImplementationKind>
    last_view_directional_implementation_;
  std::uint64_t cache_epoch_ { 1U };
  std::unordered_map<ViewId, std::uint64_t> virtual_view_generations_;
  std::unordered_map<ViewId, renderer::VirtualShadowGpuRasterInputs>
    virtual_gpu_raster_inputs_;
  std::unordered_map<ViewId, renderer::VirtualShadowFramePacket>
    virtual_frame_packets_;
  std::unordered_map<ViewId,
    std::array<VirtualShadowScheduleExtraction, frame::kFramesInFlight.get()>>
    virtual_schedule_extractions_;
  std::unordered_map<ViewId,
    std::array<VirtualShadowResolveStatsExtraction,
      frame::kFramesInFlight.get()>>
    virtual_resolve_stats_extractions_;

  std::unique_ptr<internal::ConventionalShadowBackend> conventional_backend_;
  std::unique_ptr<internal::VirtualShadowMapBackend> virtual_backend_;
};

} // namespace oxygen::renderer
