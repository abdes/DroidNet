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
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
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
    ViewId published_view_id { kInvalidViewId };
    ShaderVisibleIndex published_view_frame_bindings_slot {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex published_scene_texture_frame_slot {
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
  OXGN_VRTX_API void PrimePreparedView(RenderContext& ctx);
  OXGN_VRTX_API void OnRender(RenderContext& ctx);
  OXGN_VRTX_API void OnCompositing(RenderContext& ctx);
  OXGN_VRTX_API void OnFrameEnd(const engine::FrameContext& frame);

  OXGN_VRTX_API void ApplyStage3DepthPrepassState();
  OXGN_VRTX_API void ApplyStage9BasePassState();
  OXGN_VRTX_API void ApplyStage10RebuildState();
  OXGN_VRTX_API void ApplyStage22PostProcessState();
  OXGN_VRTX_API void ApplyStage23ExtractionState();

  [[nodiscard]] OXGN_VRTX_API auto GetSceneTextures() const
    -> const SceneTextures&;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneTextures() -> SceneTextures&;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneTextureBindings() const
    -> const SceneTextureBindings&;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneTextureExtracts() const
    -> const SceneTextureExtracts&;
  [[nodiscard]] OXGN_VRTX_API auto GetResolvedSceneColorTexture() const
    -> std::shared_ptr<graphics::Texture>;
  [[nodiscard]] OXGN_VRTX_API auto GetDefaultShadingMode() const -> ShadingMode;
  [[nodiscard]] OXGN_VRTX_API auto GetEffectiveShadingMode(
    const RenderContext& ctx) const -> ShadingMode;
  [[nodiscard]] OXGN_VRTX_API auto GetPublishedViewFrameBindings() const
    -> const ViewFrameBindings&;
  [[nodiscard]] OXGN_VRTX_API auto GetPublishedViewFrameBindingsSlot() const
    -> ShaderVisibleIndex;
  [[nodiscard]] OXGN_VRTX_API auto GetPublishedViewId() const -> ViewId;
  [[nodiscard]] OXGN_VRTX_API auto GetLastDeferredLightingState() const
    -> const DeferredLightingState&;
  [[nodiscard]] OXGN_VRTX_API static auto GetAuthoredStageOrder()
    -> const StageOrder&;
  OXGN_VRTX_API void PublishViewFrameBindings(
    ViewId view_id, const ViewFrameBindings& bindings, ShaderVisibleIndex slot);
  OXGN_VRTX_API void InvalidatePublishedViewFrameBindings();

private:
  struct ExtractArtifact {
    std::shared_ptr<graphics::Texture> texture;
  };

  OXGN_VRTX_API void RefreshSceneTextureBindings();
  OXGN_VRTX_API void ResetExtractArtifacts();
  OXGN_VRTX_API auto EnsureArtifactTexture(ExtractArtifact& artifact,
    std::string_view debug_name, const graphics::Texture& source)
    -> graphics::Texture*;
  [[nodiscard]] OXGN_VRTX_API auto ResolveVelocitySourceTexture() const
    -> const graphics::Texture*;
  OXGN_VRTX_API auto RegisterSceneTextureView(graphics::Texture& texture,
    const graphics::TextureViewDescription& desc) -> std::uint32_t;
  [[nodiscard]] OXGN_VRTX_API auto ResolveShadingModeForCurrentView(
    const RenderContext& ctx) const -> ShadingMode;
  OXGN_VRTX_API void RenderDeferredLighting(
    RenderContext& ctx, const SceneTextures& scene_textures);
  OXGN_VRTX_API void ResolveSceneColor(RenderContext& ctx);
  OXGN_VRTX_API void PostRenderCleanup(RenderContext& ctx);

  Renderer& renderer_;
  Graphics& gfx_;
  SceneTextures scene_textures_;
  SceneTextureSetupMode setup_mode_ {};
  SceneTextureBindings scene_texture_bindings_ {};
  SceneTextureExtracts scene_texture_extracts_ {};
  ExtractArtifact resolved_scene_color_artifact_ {};
  ExtractArtifact resolved_scene_depth_artifact_ {};
  ExtractArtifact prev_scene_depth_artifact_ {};
  ExtractArtifact prev_velocity_artifact_ {};
  std::shared_ptr<graphics::Buffer> deferred_light_constants_buffer_ {};
  void* deferred_light_constants_mapped_ptr_ { nullptr };
  std::vector<ShaderVisibleIndex> deferred_light_constants_indices_ {};
  std::uint32_t deferred_light_constants_slot_count_ { 0U };
  std::shared_ptr<graphics::Framebuffer>
    deferred_light_directional_framebuffer_ {};
  std::shared_ptr<graphics::Framebuffer> deferred_light_local_framebuffer_ {};
  ShadingMode default_shading_mode_ { ShadingMode::kForward };
  ViewFrameBindings published_view_frame_bindings_ {};
  ShaderVisibleIndex published_view_frame_bindings_slot_ {
    kInvalidShaderVisibleIndex
  };
  ViewId published_view_id_ { kInvalidViewId };
  DeferredLightingState deferred_lighting_state_ {};
  std::unique_ptr<InitViewsModule> init_views_;
  std::unique_ptr<DepthPrepassModule> depth_prepass_;
  std::unique_ptr<BasePassModule> base_pass_;
};

} // namespace oxygen::vortex
