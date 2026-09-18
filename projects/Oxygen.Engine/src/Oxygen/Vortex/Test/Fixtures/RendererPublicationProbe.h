//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereCameraAerialPerspectivePass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>
#include <Oxygen/Vortex/Internal/PreviousViewHistoryCache.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Passes/GroundGridPass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::testing {

struct RendererPublicationProbe {
  static auto SceneTexturePoolCounts(const SceneRenderer& renderer)
    -> std::pair<std::size_t, std::size_t>
  {
    return { renderer.scene_texture_pool_.GetAllocationCount(),
      renderer.scene_texture_pool_.GetLiveLeaseCount() };
  }

  static auto BasePassDrawCommands(const SceneRenderer& renderer)
    -> std::span<const BasePassDrawCommand>
  {
    return renderer.base_pass_->mesh_processor_->GetDrawCommands();
  }
  using FogPassConstants = environment::VolumetricFogPass::PassConstants;
  static auto EnvironmentTextures(SceneRenderer& renderer, ViewId view)
    -> std::vector<std::shared_ptr<graphics::Texture>>
  {
    auto result = std::vector<std::shared_ptr<graphics::Texture>> {};
    auto* environment = renderer.environment_.get();
    if (!environment)
      return result;
    if (environment->sky_view_lut_pass_)
      for (const auto& texture :
        environment->sky_view_lut_pass_->live_textures_)
        result.push_back(texture);
    if (environment->camera_aerial_perspective_pass_)
      for (const auto& texture :
        environment->camera_aerial_perspective_pass_->live_textures_)
        result.push_back(texture);
    if (environment->volumetric_fog_pass_) {
      for (const auto& texture :
        environment->volumetric_fog_pass_->live_textures_)
        result.push_back(texture);
      const auto history
        = environment->volumetric_fog_pass_->history_by_view_.find(view);
      if (history != environment->volumetric_fog_pass_->history_by_view_.end())
        result.push_back(history->second.texture);
    }
    return result;
  }
  static auto PublishGroundGridConstants(
    GroundGridPass& pass, const RenderContext& ctx) -> ShaderVisibleIndex
  {
    return pass.UpdatePassConstants(ctx);
  }
  static auto PublishWireframeConstants(BasePassModule& pass, Graphics& gfx,
    const RenderContext& ctx, bool pre_exposed) -> ShaderVisibleIndex
  {
    return pass.WriteWireframeConstants(gfx, ctx, pre_exposed);
  }
  static auto VelocityIntermediates(const BasePassModule& pass)
    -> std::array<std::shared_ptr<graphics::Texture>, 2>
  {
    return { pass.velocity_base_copy_,
      pass.velocity_motion_vector_world_offset_ };
  }
  static auto BuildStaticSkyPublication(EnvironmentLightingService& service,
    const RenderContext& ctx, const EnvironmentProbeState& state,
    const environment::SkyLightEnvironmentModel& model) -> EnvironmentStaticData
  {
    service.probe_state_ = state;
    auto products = environment::EnvironmentViewProducts {};
    products.sky_light = model;
    return service.BuildEnvironmentStaticData(ctx, products);
  }
  static auto FogHistory(SceneRenderer& renderer, ViewId view)
    -> std::pair<std::shared_ptr<graphics::Texture>,
      postprocess::ExposurePass::FrameLease>
  {
    if (!renderer.environment_ || !renderer.environment_->volumetric_fog_pass_)
      return {};
    const auto& histories
      = renderer.environment_->volumetric_fog_pass_->history_by_view_;
    const auto found = histories.find(view);
    return found == histories.end()
      ? decltype(FogHistory(renderer, view)) {}
      : std::pair { found->second.texture, found->second.frame_exposure };
  }
  static auto FogHistoryCount(const SceneRenderer& renderer) -> std::size_t
  {
    return renderer.environment_ && renderer.environment_->volumetric_fog_pass_
      ? renderer.environment_->volumetric_fog_pass_->history_by_view_.size()
      : 0U;
  }
  static auto RetainedExposureFrameCount(const PostProcessService& service)
    -> std::size_t
  {
    return static_cast<std::size_t>(
      std::ranges::count_if(service.exposure_pass_->frame_pool_,
        [](const auto& frame) { return frame.use_count() > 1; }));
  }
  static auto FrameExposureStates(const PostProcessService& service,
    frame::Slot slot) -> std::vector<postprocess::ExposurePass::StateLease>
  {
    return service.exposure_pass_->frame_states_[slot.get()];
  }
  static auto PreviousViewHistory(Renderer& renderer)
    -> internal::PreviousViewHistoryCache&
  {
    return *renderer.previous_view_history_cache_;
  }
  static auto SelectedBorrowForView(
    const PostProcessService& service, CompositionView::ViewStateHandle handle)
    -> postprocess::ExposurePass::StateLease
  {
    const auto found = service.exposure_pass_->exposure_states_.find(handle);
    return found != service.exposure_pass_->exposure_states_.end()
        && found->second.selected_borrow
      ? found->second.selected_borrow->state
      : nullptr;
  }
  static auto HasExposureViewState(const PostProcessService& service,
    CompositionView::ViewStateHandle handle) -> bool
  {
    return service.exposure_pass_->exposure_states_.contains(handle);
  }
  static auto SetExposureAssetLoader(PostProcessService& service,
    observer_ptr<content::IAssetLoader> loader) -> void
  {
    CHECK_F(
      !service.mask_binder_, "Set the test loader before mask binding starts");
    service.asset_loader_ = loader;
  }
  static auto ExposureStateForView(
    const PostProcessService& service, CompositionView::ViewStateHandle handle)
    -> postprocess::ExposurePass::StateLease
  {
    const auto found = service.exposure_pass_->exposure_states_.find(handle);
    return found == service.exposure_pass_->exposure_states_.end()
      ? nullptr
      : found->second.latest;
  }
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
