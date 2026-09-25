//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextureLeasePool.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/EnvironmentLightingState.h>
#include <Oxygen/Vortex/Types/ExposureSettingsStatus.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/ScreenHzbFrameBindings.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>
#include <Oxygen/Vortex/Types/ViewRenderStatus.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
namespace engine {
  class FrameContext;
} // namespace engine
namespace graphics {
  class Buffer;
  class Framebuffer;
} // namespace graphics
} // namespace oxygen

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

namespace internal {
  class RetainedTexturePool;
}

struct RenderContext;
class Renderer;
class InitViewsModule;
class DepthPrepassModule;
class BasePassModule;
class LightingService;
class ShadowService;
class PostProcessService;
struct ExposureSourceLoss;
class GroundGridPass;
class EnvironmentLightingService;
class ScreenHzbModule;
class OcclusionModule;
class TranslucencyModule;
namespace testing {
  struct RendererPublicationProbe;
}

class SceneRenderer {
public:
  using StageOrder = std::array<std::uint8_t, 23>; // NOLINT(*-magic-numbers)

  struct DeferredLightingState {
    bool consumed_published_scene_textures { false };
    bool accumulated_into_scene_color { false };
    bool used_outside_volume_local_lights { false };
    bool used_camera_inside_local_lights { false };
    bool used_non_perspective_local_lights { false };
    bool consumed_static_sky_light_product { false };
    std::uint32_t consumed_scene_depth_srv {
      SceneTextureBindings::kInvalidIndex,
    };
    std::uint32_t consumed_scene_color_uav {
      SceneTextureBindings::kInvalidIndex,
    };
    std::array<std::uint32_t, 4> consumed_gbuffer_srvs {
      SceneTextureBindings::kInvalidIndex,
      SceneTextureBindings::kInvalidIndex,
      SceneTextureBindings::kInvalidIndex,
      SceneTextureBindings::kInvalidIndex,
    };
    std::uint32_t directional_light_count { 0U };
    std::uint32_t static_sky_light_draw_count { 0U };
    std::uint32_t point_light_count { 0U };
    std::uint32_t spot_light_count { 0U };
    std::uint32_t local_light_count { 0U };
    std::uint32_t outside_volume_local_light_count { 0U };
    std::uint32_t camera_inside_local_light_count { 0U };
    std::uint32_t direct_local_light_pass_count { 0U };
    std::uint32_t non_perspective_local_light_count { 0U };
    bool owned_by_lighting_service { false };
    bool used_service_owned_local_light_geometry { false };
    ViewId published_view_id { kInvalidViewId };
    ShaderVisibleIndex published_view_frame_bindings_slot {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex published_scene_texture_frame_slot {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex published_lighting_frame_slot {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex published_shadow_frame_slot {
      kInvalidShaderVisibleIndex
    };
    std::vector<ShaderVisibleIndex> directional_shadow_surface_srvs;
    bool consumed_directional_shadow_product { false };
    bool directional_shadow_vsm_active { false };
    std::uint32_t directional_shadow_cascade_count { 0U };
    bool consumed_spot_shadow_product { false };
    std::uint32_t spot_shadow_count { 0U };
    ShaderVisibleIndex spot_shadow_surface_srv { kInvalidShaderVisibleIndex };
    bool consumed_point_shadow_product { false };
    std::uint32_t point_shadow_count { 0U };
    ShaderVisibleIndex point_shadow_surface_srv { kInvalidShaderVisibleIndex };
  };

  OXGN_VRTX_API explicit SceneRenderer(Renderer& renderer, Graphics& gfx,
    SceneTexturesConfig config, ShadingMode default_shading_mode);
  OXGN_VRTX_API ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  auto operator=(const SceneRenderer&) -> SceneRenderer& = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  auto operator=(SceneRenderer&&) -> SceneRenderer& = delete;

  OXGN_VRTX_API void OnFrameStart(const engine::FrameContext& frame);
  OXGN_VRTX_API void OnStandaloneFrameStart(frame::SequenceNumber sequence,
    frame::Slot slot, std::optional<glm::uvec2> frame_extent);
  OXGN_VRTX_API void OnPreRender(const engine::FrameContext& frame);
  OXGN_VRTX_API void PrimePreparedViews(RenderContext& ctx);
  OXGN_VRTX_API auto PrepareExposureDomain(
    RenderContext& ctx, graphics::CommandRecorder& recorder) -> bool;
  OXGN_VRTX_API void PrimePreparedView(RenderContext& ctx);
  OXGN_VRTX_API void RenderViewFamily(RenderContext& ctx);
  OXGN_VRTX_API auto OnRender(RenderContext& ctx) -> bool;
  [[nodiscard]] OXGN_VRTX_API auto InspectViewRenderStatus(ViewId view_id) const
    -> std::optional<ViewRenderStatus>;
  [[nodiscard]] auto ResolveViewLightingFrameSlot(ViewId view_id) const
    -> ShaderVisibleIndex;
  OXGN_VRTX_API void OnCompositing(RenderContext& ctx);
  OXGN_VRTX_API void OnFrameEnd(const engine::FrameContext& frame);
  OXGN_VRTX_API void RemoveViewState(ViewId view_id,
    CompositionView::ViewStateHandle view_state_handle
    = CompositionView::kInvalidViewStateHandle);
  OXGN_VRTX_API void PreserveRemovedExposureSource(
    std::shared_ptr<const ExposureSourceLoss> loss);

  OXGN_VRTX_API void PublishDepthPrepassProducts();
  OXGN_VRTX_API void PublishScreenHzbProducts(RenderContext& ctx);
  OXGN_VRTX_API void PublishBasePassVelocity();
  OXGN_VRTX_API void PublishDeferredBasePassSceneTextures(RenderContext& ctx);
  OXGN_VRTX_API void PublishCustomDepthProducts();
  OXGN_VRTX_API void FinalizeSceneTextureExtractions();

  OXGN_VRTX_NDAPI auto GetSceneTextures() const -> const SceneTextures&;
  OXGN_VRTX_NDAPI auto GetSceneTextures() -> SceneTextures&;
  OXGN_VRTX_NDAPI auto GetSceneTextureBindings() const
    -> const SceneTextureBindings&;
  OXGN_VRTX_NDAPI auto GetSceneTextureExtracts() const
    -> const SceneTextureExtracts&;
  OXGN_VRTX_NDAPI auto GetResolvedSceneColorTexture() const
    -> std::shared_ptr<graphics::Texture>;
  OXGN_VRTX_NDAPI auto GetDefaultShadingMode() const -> ShadingMode;
  OXGN_VRTX_NDAPI auto GetEffectiveShadingMode(const RenderContext& ctx) const
    -> ShadingMode;
  OXGN_VRTX_NDAPI auto GetPublishedViewFrameBindings() const
    -> const ViewFrameBindings&;
  OXGN_VRTX_NDAPI auto GetPublishedScreenHzbBindings() const
    -> const ScreenHzbFrameBindings&;
  OXGN_VRTX_NDAPI auto GetPublishedViewFrameBindingsSlot() const
    -> ShaderVisibleIndex;
  OXGN_VRTX_NDAPI auto GetPublishedViewId() const -> ViewId;
  OXGN_VRTX_NDAPI auto GetLastDeferredLightingState() const
    -> const DeferredLightingState&;
  [[nodiscard]] OXGN_VRTX_API auto InspectExposureSettings(
    CompositionView::ViewStateHandle handle) const
    -> std::optional<ExposureSettingsStatus>;
  OXGN_VRTX_NDAPI auto GetLastEnvironmentLightingState() const
    -> const EnvironmentLightingState&;
  OXGN_VRTX_NDAPI static auto GetAuthoredStageOrder() -> const StageOrder&;
  OXGN_VRTX_API void PublishViewFrameBindings(
    ViewId view_id, const ViewFrameBindings& bindings, ShaderVisibleIndex slot);
  OXGN_VRTX_API void InvalidatePublishedViewFrameBindings();

private:
  auto ReportLightingFailure(
    LightingPreparationFailure failure, ViewId fallback_view) -> void;
  std::unordered_map<ViewId, LightingPreparationFailure>
    reported_lighting_failures_;
  std::unordered_map<ViewId, ViewRenderStatus> view_render_status_;
  friend struct testing::RendererPublicationProbe;

  struct ExposureProductLayout {
    std::array<std::array<std::uint32_t, 6U>, 4U> products {};
    std::uint64_t revision { 0U };
    bool fp32_only { false };
  };
  std::unordered_map<CompositionView::ViewStateHandle, ExposureProductLayout>
    exposure_product_layouts_;
  auto DescribeExposureProductLayout(const RenderContext& ctx)
    -> ExposureProductLayout;

  struct ExtractArtifact {
    ExtractArtifact();
    ~ExtractArtifact();
    ExtractArtifact(const ExtractArtifact&) = delete;
    auto operator=(const ExtractArtifact&) -> ExtractArtifact& = delete;
    ExtractArtifact(ExtractArtifact&&) noexcept;
    auto operator=(ExtractArtifact&&) noexcept -> ExtractArtifact&;
    std::shared_ptr<graphics::Texture> texture;
    std::unique_ptr<internal::RetainedTexturePool> pool;
  };

  OXGN_VRTX_API void RefreshSceneTextureBindings();
  OXGN_VRTX_API void BeginFrame(frame::SequenceNumber sequence,
    frame::Slot slot, std::optional<glm::uvec2> frame_extent);
  OXGN_VRTX_API void ResetExtractArtifacts();
  OXGN_VRTX_API void ResizeSceneTextureFamily(glm::uvec2 new_extent);
  OXGN_VRTX_API void ResetPerViewSceneProducts();
  OXGN_VRTX_API auto ActiveSceneTextures() -> SceneTextures&;
  OXGN_VRTX_NDAPI auto ActiveSceneTextures() const -> const SceneTextures&;
  OXGN_VRTX_NDAPI auto BuildSceneTextureLeaseKey(const RenderContext& ctx) const
    -> SceneTextureLeaseKey;
  OXGN_VRTX_API void BindPreparedView(RenderContext& ctx);
  OXGN_VRTX_API auto RenderCurrentView(
    RenderContext& ctx, graphics::CommandRecorder& recorder) -> bool;
  OXGN_VRTX_API auto EnsureArtifactTexture(RenderContext& ctx,
    ExtractArtifact& artifact, std::string_view debug_name,
    const graphics::Texture& source, std::optional<Format> format = {})
    -> graphics::Texture*;
  OXGN_VRTX_NDAPI auto ResolveVelocitySourceTexture() const
    -> const graphics::Texture*;
  OXGN_VRTX_API auto RegisterSceneTextureView(graphics::Texture& texture,
    const graphics::TextureViewDescription& desc) -> std::uint32_t;
  OXGN_VRTX_NDAPI auto ResolveShadingModeForCurrentView(
    const RenderContext& ctx) const -> ShadingMode;
  OXGN_VRTX_API auto RenderDebugVisualization(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const SceneTextures& scene_textures)
    -> bool;
  OXGN_VRTX_API auto RenderDeferredLighting(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const SceneTextures& scene_textures)
    -> bool;
  OXGN_VRTX_API void ResolveSceneColor(RenderContext& ctx,
    graphics::CommandRecorder& recorder,
    const PostProcessService::PreparedExposure* prepared = nullptr);
  OXGN_VRTX_API void PostRenderCleanup(
    RenderContext& ctx, graphics::CommandRecorder& recorder);

  Renderer& renderer_;
  Graphics& gfx_;
  ViewFrameBindings published_view_frame_bindings_ {};
  ScreenHzbFrameBindings published_screen_hzb_bindings_ {};
  SceneTextures scene_textures_;
  SceneTextureLeasePool scene_texture_pool_;
  std::unique_ptr<internal::RetainedTexturePool> scene_color_pool_;
  SceneTextures* active_scene_textures_ { nullptr };
  SceneTextures* inspected_scene_textures_ { nullptr };
  SceneTextureSetupMode setup_mode_ {};
  SceneTextureBindings scene_texture_bindings_ {};
  SceneTextureExtracts scene_texture_extracts_ {};
  std::shared_ptr<SceneTextureLease> active_scene_texture_lease_;
  ExtractArtifact resolved_scene_color_artifact_ {};
  ExtractArtifact resolved_scene_depth_artifact_ {};
  ExtractArtifact prev_velocity_artifact_ {};
  std::shared_ptr<graphics::Framebuffer> debug_visualization_framebuffer_;
  ViewId published_view_id_ { kInvalidViewId };
  ShaderVisibleIndex published_view_frame_bindings_slot_ {
    kInvalidShaderVisibleIndex
  };
  ShadingMode default_shading_mode_ { ShadingMode::kForward };
  DeferredLightingState deferred_lighting_state_ {};
  EnvironmentLightingState environment_lighting_state_ {};
  FrameLightSelection frame_light_selection_ {};
  std::vector<PreparedViewLightingInput> frame_lighting_views_;
  std::vector<PreparedViewShadowInput> frame_shadow_views_;
  std::vector<PreparedViewShadowInput> frame_shadow_preparation_views_;
  std::optional<frame::SequenceNumber> lighting_grid_built_sequence_;
  std::unique_ptr<InitViewsModule> init_views_;
  std::unique_ptr<DepthPrepassModule> depth_prepass_;
  std::unique_ptr<ScreenHzbModule> screen_hzb_;
  std::unique_ptr<OcclusionModule> occlusion_;
  std::unique_ptr<BasePassModule> base_pass_;
  std::unique_ptr<TranslucencyModule> translucency_;
  std::unique_ptr<LightingService> lighting_;
  std::unique_ptr<ShadowService> shadows_;
  std::unique_ptr<EnvironmentLightingService> environment_;
  std::unique_ptr<GroundGridPass> ground_grid_pass_;
  std::unique_ptr<PostProcessService> post_process_;
};

} // namespace oxygen::vortex
