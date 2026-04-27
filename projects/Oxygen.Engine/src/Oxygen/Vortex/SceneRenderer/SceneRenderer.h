//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextureLeasePool.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/ScreenHzbFrameBindings.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>
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

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class InitViewsModule;
class DepthPrepassModule;
class BasePassModule;
class LightingService;
class ShadowService;
class PostProcessService;
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
    std::uint32_t consumed_scene_depth_srv {
      SceneTextureBindings::kInvalidIndex
    };
    std::uint32_t consumed_scene_color_uav {
      SceneTextureBindings::kInvalidIndex
    };
    std::array<std::uint32_t, 4> consumed_gbuffer_srvs {
      SceneTextureBindings::kInvalidIndex,
      SceneTextureBindings::kInvalidIndex,
      SceneTextureBindings::kInvalidIndex,
      SceneTextureBindings::kInvalidIndex,
    };
    std::uint32_t directional_light_count { 0U };
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
    ShaderVisibleIndex directional_shadow_surface_srv {
      kInvalidShaderVisibleIndex
    };
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

  struct EnvironmentLightingState {
    bool published_bindings { false };
    bool owned_by_environment_service { false };
    bool stage14_requested { false };
    bool stage14_local_fog_requested { false };
    bool stage14_local_fog_executed { false };
    bool stage14_local_fog_hzb_consumed { false };
    bool stage14_local_fog_hzb_unavailable { false };
    bool stage14_local_fog_buffer_ready { false };
    bool stage14_local_fog_skipped { false };
    std::uint32_t stage14_local_fog_instance_count { 0U };
    std::uint32_t stage14_local_fog_dispatch_count_x { 0U };
    std::uint32_t stage14_local_fog_dispatch_count_y { 0U };
    std::uint32_t stage14_local_fog_dispatch_count_z { 0U };
    bool stage14_volumetric_fog_requested { false };
    bool stage14_volumetric_fog_executed { false };
    bool stage14_integrated_light_scattering_valid { false };
    ShaderVisibleIndex stage14_integrated_light_scattering_srv {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t stage14_volumetric_fog_grid_width { 0U };
    std::uint32_t stage14_volumetric_fog_grid_height { 0U };
    std::uint32_t stage14_volumetric_fog_grid_depth { 0U };
    std::uint32_t stage14_volumetric_fog_dispatch_count_x { 0U };
    std::uint32_t stage14_volumetric_fog_dispatch_count_y { 0U };
    std::uint32_t stage14_volumetric_fog_dispatch_count_z { 0U };
    bool stage14_volumetric_fog_height_fog_media_requested { false };
    bool stage14_volumetric_fog_height_fog_media_executed { false };
    bool stage14_volumetric_fog_sky_light_injection_requested { false };
    bool stage14_volumetric_fog_sky_light_injection_executed { false };
    bool stage14_volumetric_fog_temporal_history_requested { false };
    bool stage14_volumetric_fog_temporal_history_reprojection_executed {
      false
    };
    bool stage14_volumetric_fog_temporal_history_reset { false };
    bool stage14_volumetric_fog_local_fog_injection_requested { false };
    bool stage14_volumetric_fog_local_fog_injection_executed { false };
    std::uint32_t stage14_volumetric_fog_local_fog_instance_count { 0U };
    bool stage15_requested { false };
    bool sky_requested { false };
    bool sky_executed { false };
    std::uint32_t sky_draw_count { 0U };
    bool atmosphere_requested { false };
    bool atmosphere_executed { false };
    std::uint32_t atmosphere_draw_count { 0U };
    bool fog_requested { false };
    bool fog_executed { false };
    std::uint32_t fog_draw_count { 0U };
    std::uint32_t total_draw_count { 0U };
    bool ambient_bridge_published { false };
    std::uint32_t probe_revision { 0U };
    ShaderVisibleIndex published_environment_frame_slot {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex ambient_bridge_irradiance_srv {
      kInvalidShaderVisibleIndex
    };
  };

  OXGN_VRTX_API explicit SceneRenderer(Renderer& renderer, Graphics& gfx,
    SceneTexturesConfig config, ShadingMode default_shading_mode);
  OXGN_VRTX_API ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  auto operator=(const SceneRenderer&) -> SceneRenderer& = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  auto operator=(SceneRenderer&&) -> SceneRenderer& = delete;

  OXGN_VRTX_API void OnFrameStart(const engine::FrameContext& frame);
  OXGN_VRTX_API void OnPreRender(const engine::FrameContext& frame);
  OXGN_VRTX_API void PrimePreparedViews(RenderContext& ctx);
  OXGN_VRTX_API void PrimePreparedView(RenderContext& ctx);
  OXGN_VRTX_API void RenderViewFamily(RenderContext& ctx);
  OXGN_VRTX_API void OnRender(RenderContext& ctx);
  OXGN_VRTX_API void OnCompositing(RenderContext& ctx);
  OXGN_VRTX_API void OnFrameEnd(const engine::FrameContext& frame);
  OXGN_VRTX_API void RemoveViewState(ViewId view_id,
    CompositionView::ViewStateHandle view_state_handle
    = CompositionView::kInvalidViewStateHandle);

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
  OXGN_VRTX_NDAPI auto GetLastEnvironmentLightingState() const
    -> const EnvironmentLightingState&;
  OXGN_VRTX_NDAPI static auto GetAuthoredStageOrder() -> const StageOrder&;
  OXGN_VRTX_API void PublishViewFrameBindings(
    ViewId view_id, const ViewFrameBindings& bindings, ShaderVisibleIndex slot);
  OXGN_VRTX_API void InvalidatePublishedViewFrameBindings();

private:
  friend struct testing::RendererPublicationProbe;

  struct ExtractArtifact {
    std::shared_ptr<graphics::Texture> texture;
  };

  OXGN_VRTX_API void RefreshSceneTextureBindings();
  OXGN_VRTX_API void ResetExtractArtifacts();
  OXGN_VRTX_API void ResizeSceneTextureFamily(glm::uvec2 new_extent);
  OXGN_VRTX_API void ResetPerViewSceneProducts();
  OXGN_VRTX_API auto ActiveSceneTextures() -> SceneTextures&;
  OXGN_VRTX_NDAPI auto ActiveSceneTextures() const -> const SceneTextures&;
  OXGN_VRTX_NDAPI auto BuildSceneTextureLeaseKey(
    const RenderContext& ctx) const -> SceneTextureLeaseKey;
  OXGN_VRTX_API void BindPreparedView(RenderContext& ctx);
  OXGN_VRTX_API void RenderCurrentView(RenderContext& ctx);
  OXGN_VRTX_API auto EnsureArtifactTexture(ExtractArtifact& artifact,
    std::string_view debug_name, const graphics::Texture& source)
    -> graphics::Texture*;
  OXGN_VRTX_NDAPI auto ResolveVelocitySourceTexture() const
    -> const graphics::Texture*;
  OXGN_VRTX_API auto RegisterSceneTextureView(graphics::Texture& texture,
    const graphics::TextureViewDescription& desc) -> std::uint32_t;
  OXGN_VRTX_NDAPI auto ResolveShadingModeForCurrentView(
    const RenderContext& ctx) const -> ShadingMode;
  OXGN_VRTX_API auto RenderDebugVisualization(
    RenderContext& ctx, const SceneTextures& scene_textures) -> bool;
  OXGN_VRTX_API void RenderDeferredLighting(
    RenderContext& ctx, const SceneTextures& scene_textures);
  OXGN_VRTX_API void ResolveSceneColor(RenderContext& ctx);
  OXGN_VRTX_API void PostRenderCleanup(RenderContext& ctx);

  Renderer& renderer_;
  Graphics& gfx_;
  ViewFrameBindings published_view_frame_bindings_ {};
  ScreenHzbFrameBindings published_screen_hzb_bindings_ {};
  SceneTextures scene_textures_;
  SceneTextureLeasePool scene_texture_pool_;
  SceneTextures* active_scene_textures_ { nullptr };
  SceneTextures* inspected_scene_textures_ { nullptr };
  SceneTextureSetupMode setup_mode_ {};
  SceneTextureBindings scene_texture_bindings_ {};
  SceneTextureExtracts scene_texture_extracts_ {};
  ExtractArtifact resolved_scene_color_artifact_ {};
  ExtractArtifact resolved_scene_depth_artifact_ {};
  ExtractArtifact prev_scene_depth_artifact_ {};
  ExtractArtifact prev_velocity_artifact_ {};
  std::shared_ptr<graphics::Framebuffer> debug_visualization_framebuffer_ {};
  ViewId published_view_id_ { kInvalidViewId };
  ShaderVisibleIndex published_view_frame_bindings_slot_ {
    kInvalidShaderVisibleIndex
  };
  ShadingMode default_shading_mode_ { ShadingMode::kForward };
  DeferredLightingState deferred_lighting_state_ {};
  EnvironmentLightingState environment_lighting_state_ {};
  FrameLightSelection frame_light_selection_ {};
  std::vector<PreparedViewLightingInput> frame_lighting_views_ {};
  std::vector<PreparedViewShadowInput> frame_shadow_views_ {};
  frame::SequenceNumber lighting_grid_built_sequence_ { 0U };
  frame::SequenceNumber shadow_depths_built_sequence_ { 0U };
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
