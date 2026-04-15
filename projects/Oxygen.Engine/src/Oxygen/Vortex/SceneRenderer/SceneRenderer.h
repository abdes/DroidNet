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

  OXGN_VRTX_API explicit SceneRenderer(Renderer& renderer, Graphics& gfx,
    SceneTexturesConfig config, ShadingMode default_shading_mode);
  OXGN_VRTX_API ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  auto operator=(const SceneRenderer&) -> SceneRenderer& = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  auto operator=(SceneRenderer&&) -> SceneRenderer& = delete;

  OXGN_VRTX_API void OnFrameStart(const engine::FrameContext& frame);
  OXGN_VRTX_API void OnPreRender(const engine::FrameContext& frame);
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
  [[nodiscard]] OXGN_VRTX_API auto GetDefaultShadingMode() const -> ShadingMode;
  [[nodiscard]] OXGN_VRTX_API auto GetEffectiveShadingMode(
    const RenderContext& ctx) const -> ShadingMode;
  [[nodiscard]] OXGN_VRTX_API auto GetPublishedViewFrameBindings() const
    -> const ViewFrameBindings&;
  [[nodiscard]] OXGN_VRTX_API auto GetPublishedViewFrameBindingsSlot() const
    -> ShaderVisibleIndex;
  [[nodiscard]] OXGN_VRTX_API auto GetPublishedViewId() const -> ViewId;
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
  ShadingMode default_shading_mode_ { ShadingMode::kForward };
  ViewFrameBindings published_view_frame_bindings_ {};
  ShaderVisibleIndex published_view_frame_bindings_slot_ {
    kInvalidShaderVisibleIndex
  };
  ViewId published_view_id_ { kInvalidViewId };
  std::unique_ptr<InitViewsModule> init_views_;
  std::unique_ptr<DepthPrepassModule> depth_prepass_;
  std::unique_ptr<BasePassModule> base_pass_;
};

} // namespace oxygen::vortex
