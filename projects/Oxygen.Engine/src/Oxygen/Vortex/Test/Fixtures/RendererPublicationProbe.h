//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::testing {

struct RendererPublicationProbe {
  static auto EnqueueExposureStatus(PostProcessService& service,
    const ExposureTransitionToken& token,
    postprocess::ExposurePass::StateLease state, const RenderContext& ctx,
    std::uint64_t settings_revision) -> void
  {
    service.EnqueueExposureStatus(
      token, std::move(state), ctx, settings_revision);
  }

  static auto ExposureStatusCounts(
    const PostProcessService& service, CompositionView::ViewStateHandle handle)
    -> std::pair<std::size_t, std::size_t>
  {
    const auto pending = service.pending_exposure_status_.find(handle);
    return { pending == service.pending_exposure_status_.end()
        ? 0U
        : pending->second.size(),
      service.deferred_exposure_status_.contains(handle) ? 1U : 0U };
  }

  static auto GetPostProcessService(SceneRenderer& renderer)
    -> PostProcessService*
  {
    return renderer.post_process_.get();
  }

  static auto GetSceneRenderer(Renderer& renderer) -> SceneRenderer*
  {
    return renderer.scene_renderer_.get();
  }

  static auto GetSceneRenderer(const Renderer& renderer) -> const SceneRenderer*
  {
    return renderer.scene_renderer_.get();
  }

  static auto GetLightingService(Renderer& renderer) -> LightingService*
  {
    return renderer.scene_renderer_ != nullptr
      ? renderer.scene_renderer_->lighting_.get()
      : nullptr;
  }

  static auto GetLightingService(const Renderer& renderer)
    -> const LightingService*
  {
    return renderer.scene_renderer_ != nullptr
      ? renderer.scene_renderer_->lighting_.get()
      : nullptr;
  }

  static auto GetLightingService(SceneRenderer& scene_renderer)
    -> LightingService*
  {
    return scene_renderer.lighting_.get();
  }

  static auto GetLightingService(const SceneRenderer& scene_renderer)
    -> const LightingService*
  {
    return scene_renderer.lighting_.get();
  }

  static auto GetShadowService(SceneRenderer& scene_renderer) -> ShadowService*
  {
    return scene_renderer.shadows_.get();
  }

  static auto GetShadowService(const SceneRenderer& scene_renderer)
    -> const ShadowService*
  {
    return scene_renderer.shadows_.get();
  }

  static auto GetFrameLightSelection(SceneRenderer& scene_renderer)
    -> FrameLightSelection&
  {
    return scene_renderer.frame_light_selection_;
  }

  static auto GetFrameLightSelection(const SceneRenderer& scene_renderer)
    -> const FrameLightSelection&
  {
    return scene_renderer.frame_light_selection_;
  }

  static auto GetViewConstants(const Renderer& renderer) -> const ViewConstants&
  {
    return renderer.view_const_cpu_;
  }

  static auto GetViewConstantsManager(const Renderer& renderer)
    -> const internal::ViewConstantsManager*
  {
    return renderer.view_const_manager_.get();
  }

  static auto PopulateRenderContextViewState(Renderer& renderer,
    RenderContext& render_context, engine::FrameContext& context,
    const bool prefer_composite_source) -> void
  {
    renderer.PopulateRenderContextViewState(
      render_context, context, prefer_composite_source);
  }
};

} // namespace oxygen::vortex::testing
