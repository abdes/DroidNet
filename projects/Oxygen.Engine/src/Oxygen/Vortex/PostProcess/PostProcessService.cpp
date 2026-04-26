//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/PostProcess/Passes/BloomPass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex {

PostProcessService::PostProcessService(Renderer& renderer)
  : renderer_(renderer)
  , exposure_pass_(std::make_unique<postprocess::ExposurePass>(renderer))
  , bloom_pass_(std::make_unique<postprocess::BloomPass>(renderer))
  , tonemap_pass_(std::make_unique<postprocess::TonemapPass>(renderer))
{
}

PostProcessService::~PostProcessService() = default;

auto PostProcessService::EnsurePublishResources() -> bool
{
  if (bindings_publisher_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  bindings_publisher_ = std::make_unique<
    internal::PerViewStructuredPublisher<PostProcessFrameBindings>>(
    observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
    observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
    "PostProcessFrameBindings");
  return true;
}

auto PostProcessService::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  published_views_.clear();
  last_execution_state_ = {};
  if (EnsurePublishResources()) {
    bindings_publisher_->OnFrameStart(sequence, slot);
  }
}

auto PostProcessService::SetConfig(const PostProcessConfig& config) -> void
{
  config_ = config;
}

auto PostProcessService::BuildBindings(const Inputs& inputs) const
  -> PostProcessFrameBindings
{
  return {
    .resolved_scene_color_srv = inputs.scene_signal_srv,
    .scene_depth_srv = inputs.scene_depth_srv,
    .scene_velocity_srv = inputs.scene_velocity_srv,
    .bloom_texture_srv = inputs.bloom_texture_srv,
    .eye_adaptation_srv = inputs.eye_adaptation_srv,
    .eye_adaptation_uav = inputs.eye_adaptation_uav,
    .post_history_srv = inputs.post_history_srv,
    .tone_mapper = config_.tone_mapper,
    .metering_mode = config_.metering_mode,
    .enable_bloom = config_.enable_bloom ? 1U : 0U,
    .enable_auto_exposure = config_.enable_auto_exposure ? 1U : 0U,
    .fixed_exposure = config_.fixed_exposure,
    .gamma = config_.gamma,
    .bloom_intensity = config_.bloom_intensity,
    .bloom_threshold = config_.bloom_threshold,
    .auto_exposure_speed_up = config_.auto_exposure_speed_up,
    .auto_exposure_speed_down = config_.auto_exposure_speed_down,
    .auto_exposure_low_percentile = config_.auto_exposure_low_percentile,
    .auto_exposure_high_percentile = config_.auto_exposure_high_percentile,
    .auto_exposure_min_ev = config_.auto_exposure_min_ev,
    .auto_exposure_max_ev = config_.auto_exposure_max_ev,
    .auto_exposure_min_log_luminance = config_.auto_exposure_min_log_luminance,
    .auto_exposure_log_luminance_range
    = config_.auto_exposure_log_luminance_range,
    .auto_exposure_target_luminance = config_.auto_exposure_target_luminance,
    .auto_exposure_spot_meter_radius = config_.auto_exposure_spot_meter_radius,
  };
}

auto PostProcessService::PublishBindings(const ViewId view_id,
  const PostProcessFrameBindings& bindings) -> ShaderVisibleIndex
{
  if (!EnsurePublishResources()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto slot = bindings_publisher_->Publish(view_id, bindings);
  published_views_.insert_or_assign(
    view_id, PublishedView { .slot = slot, .bindings = bindings });
  return slot;
}

auto PostProcessService::Execute(const ViewId view_id, RenderContext& ctx,
  const SceneTextures& scene_textures, const Inputs& inputs) -> void
{
  const auto exposure = exposure_pass_->Execute(ctx, config_,
    postprocess::ExposurePass::Inputs {
      .scene_signal = inputs.scene_signal,
      .scene_signal_srv = inputs.scene_signal_srv,
    });
  auto bindings = BuildBindings(inputs);
  bindings.eye_adaptation_srv = exposure.exposure_buffer_srv;
  bindings.eye_adaptation_uav = exposure.exposure_buffer_uav;
  const auto slot = PublishBindings(view_id, bindings);
  const auto bloom = bloom_pass_->Execute(config_, bindings);
  const auto tonemap = tonemap_pass_->Record(ctx, scene_textures,
    postprocess::TonemapPass::Inputs {
      .scene_signal = inputs.scene_signal,
      .exposure_buffer = exposure.exposure_buffer,
      .scene_signal_srv = inputs.scene_signal_srv,
      .bloom_texture_srv = bloom.bloom_texture_srv,
      .exposure_buffer_srv = exposure.exposure_buffer_srv,
      .post_target = inputs.post_target,
      .tone_mapper = config_.tone_mapper,
      .exposure_value = exposure.exposure_value,
      .gamma = config_.gamma,
      .bloom_intensity = config_.bloom_intensity,
    });

  last_execution_state_ = {
    .published_bindings = slot != kInvalidShaderVisibleIndex,
    .tonemap_requested = tonemap.requested,
    .tonemap_executed = tonemap.executed,
    .wrote_visible_output = tonemap.wrote_visible_output,
    .bloom_requested = bloom.requested,
    .bloom_executed = bloom.executed,
    .auto_exposure_requested = exposure.requested,
    .auto_exposure_executed = exposure.executed,
    .used_fixed_exposure = exposure.used_fixed_exposure,
    .view_id = view_id,
    .post_process_frame_slot = slot,
    .exposure_value = exposure.exposure_value,
  };
}

auto PostProcessService::RemoveViewState(const ViewId view_id) -> void
{
  published_views_.erase(view_id);
  exposure_pass_->RemoveViewState(view_id);
}

auto PostProcessService::InspectBindings(const ViewId view_id) const
  -> const PostProcessFrameBindings*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.bindings : nullptr;
}

auto PostProcessService::ResolveBindingSlot(const ViewId view_id) const
  -> ShaderVisibleIndex
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end()
    ? it->second.slot
    : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
}

} // namespace oxygen::vortex
