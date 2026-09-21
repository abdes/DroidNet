//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/SubmissionCallback.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/SceneBackground.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/PostProcess/Passes/BloomPass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::vortex {

PostProcessService::PostProcessService(
  Renderer& renderer, observer_ptr<content::IAssetLoader> asset_loader)
  : renderer_(renderer)
  , asset_loader_(asset_loader)
  , exposure_pass_(std::make_unique<postprocess::ExposurePass>(renderer))
  , bloom_pass_(std::make_unique<postprocess::BloomPass>(renderer))
  , tonemap_pass_(std::make_unique<postprocess::TonemapPass>(renderer))
{
}

PostProcessService::~PostProcessService()
{
  pending_exposure_status_.clear();
  deferred_exposure_status_.clear();
  reusable_exposure_status_.clear();
  captured_exposure_settings_.clear();
  captured_exposure_sources_.clear();
  // Binder owns descriptor retirement; release all its leases first.
  exposure_settings_.clear();
  transient_exposure_settings_ = {};
  for (auto& masks : frame_masks_) {
    masks.clear();
  }
  mask_binder_.reset();
}

auto PostProcessService::EnsureMaskBinder() -> resources::TextureBinder*
{
  if (mask_binder_) {
    return mask_binder_.get();
  }
  auto gfx = renderer_.GetGraphics();
  auto loader = asset_loader_ ? asset_loader_ : renderer_.GetAssetLoader();
  if (!gfx || !loader) {
    return nullptr;
  }
  mask_binder_
    = std::make_unique<resources::TextureBinder>(observer_ptr { gfx.get() },
      observer_ptr { &renderer_.GetStagingProvider() },
      observer_ptr { &renderer_.GetUploadCoordinator() }, loader);
  mask_binder_->OnFrameStart();
  return mask_binder_.get();
}

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
  if (current_sequence_ == sequence && current_slot_ == slot) {
    return;
  }
  PollExposureStatus();
  captured_exposure_settings_.clear();
  captured_exposure_sources_.clear();
  current_sequence_ = sequence;
  current_slot_ = slot;
  exposure_pass_->OnFrameStart(sequence, slot);
  CHECK_LT_F(slot.get(), frame_masks_.size());
  frame_masks_.at(slot.get()).clear();
  if (mask_binder_) {
    mask_binder_->OnFrameStart();
  }
  published_views_.clear();
  last_execution_state_ = {};
  if (EnsurePublishResources()) {
    bindings_publisher_->OnFrameStart(sequence, slot);
  }
}

auto PostProcessService::SetConfig(
  const PostProcessConfig& config, std::optional<float> camera_ev) -> void
{
  if (!camera_ev && config.exposure.mode == engine::ExposureMode::kManualCamera
    && GetConfig().exposure.mode == engine::ExposureMode::kManualCamera) {
    camera_ev = resolved_config_.CameraEv();
  }
  auto resolved = ResolvedPostProcessConfig::Resolve(
    config, camera_ev, resolved_config_.Revision());
  if (!resolved) {
    LOG_F(ERROR, "Post-process exposure config rejected: {}",
      scene::to_string(resolved.error()));
    return;
  }
  if (config.exposure != GetConfig().exposure
    || resolved->Exposure().fixed_scale
      != resolved_config_.Exposure().fixed_scale) {
    *resolved = ResolvedPostProcessConfig(config, resolved->Exposure(),
      resolved_config_.Revision() + 1U, camera_ev);
  }
  SetResolvedConfig(*resolved);
}

auto PostProcessService::SetResolvedConfig(
  const ResolvedPostProcessConfig& config) -> void
{
  resolved_config_ = config;
}

auto PostProcessService::BuildPassConfig(const PostProcessConfig& config,
  const ViewId view_id, const CompositionView::ViewStateHandle handle) const
  -> ResolvedPostProcessConfig
{
  const auto& accepted
    = captured_exposure_settings_.at({ view_id, handle }).settings;
  return { config, accepted.resolved, accepted.revision, accepted.camera_ev };
}

auto PostProcessService::ResolveViewExposureSettings(
  const CompositionView::ViewStateHandle handle,
  const scene::ExposureSettings& requested,
  const std::optional<float> camera_ev) -> const ExposureSettingsState&
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.ResolveViewExposureSettings",
    profiling::ProfileCategory::kPass);
  auto* state = &transient_exposure_settings_;
  if (handle == CompositionView::kInvalidViewStateHandle) {
    // Stateless views retain no settings/history. Keep only the last rejected
    // request identity for bounded diagnostics across identical invocations.
    auto previous_error = transient_exposure_settings_.last_error;
    auto previous_mask = transient_exposure_settings_.requested_mask;
    auto previous_mask_error
      = std::move(transient_exposure_settings_.mask_error);
    transient_exposure_settings_ = {};
    transient_exposure_settings_.last_error = previous_error;
    transient_exposure_settings_.requested_mask = previous_mask;
    transient_exposure_settings_.mask_error = std::move(previous_mask_error);
  } else {
    state = &exposure_settings_[handle];
  }
  const auto lifetime = renderer_.EnsureExposureLifetime(handle);
  if (state->lifetime != lifetime) {
    reusable_exposure_status_.erase(handle);
    *state = {};
    state->lifetime = lifetime;
  }
  const auto candidate = scene::ResolveExposureSettings(requested, camera_ev);
  if (!candidate) {
    if (state->last_error != candidate.error()) {
      LOG_F(ERROR, "Exposure settings for view state {} rejected: {}",
        handle.get(), scene::to_string(candidate.error()));
    }
    if (state->revision == 0U) {
      // A new/stateless view cannot inherit another view's last active values.
      state->resolved
        = *scene::ResolveExposureSettings(scene::ExposureSettings {});
    }
    state->last_error = candidate.error();
    return *state;
  }
  const bool uses_mask = requested.enabled
    && requested.mode == engine::ExposureMode::kAuto
    && requested.min_ev != requested.max_ev
    && requested.metering_mask.get() != 0U;
  std::shared_ptr<const resources::TextureBinder::ReadyTexture> mask;
  if (uses_mask) {
    auto* binder = EnsureMaskBinder();
    std::string failure;
    if (binder) {
      [[maybe_unused]] const auto slot
        = binder->GetOrAllocate(requested.metering_mask);
      mask = binder->AcquireReadyTexture(requested.metering_mask);
      if (binder->HasResourceFailed(requested.metering_mask)) {
        failure = "texture load or upload failed";
      } else if (mask) {
        const auto& desc = mask->texture->GetDescriptor();
        const auto& format = graphics::detail::GetFormatInfo(desc.format);
        if (desc.texture_type != TextureType::kTexture2D
          || desc.array_size != 1U || desc.sample_count != 1U || format.is_srgb
          || format.has_depth || format.has_stencil
          || format.kind == graphics::detail::FormatKind::kInteger
          || !format.has_red) {
          failure = "mask requires a linear, single-sample 2D color texture";
          mask.reset();
        }
      }
    } else {
      failure = "texture loader is unavailable";
    }
    if (!mask) {
      if (!failure.empty()
        && (state->requested_mask != requested.metering_mask
          || state->mask_error != failure)) {
        LOG_F(ERROR, "Exposure mask {} for view state {} rejected: {}",
          requested.metering_mask.get(), handle.get(), failure);
      }
      state->requested_mask = requested.metering_mask;
      state->mask_error = std::move(failure);
      state->mask_status = state->mask_error.empty()
        ? ExposureMaskStatus::kPending
        : ExposureMaskStatus::kFailed;
      if (state->revision == 0U) {
        state->resolved
          = *scene::ResolveExposureSettings(scene::ExposureSettings {});
      }
      state->last_error.reset();
      return *state;
    }
  }
  state->mask = std::move(mask);
  state->mask_status
    = uses_mask ? ExposureMaskStatus::kReady : ExposureMaskStatus::kAbsent;
  state->requested_mask = requested.metering_mask;
  state->mask_error.clear();
  if (state->revision == 0U || state->resolved.authored != requested
    || state->resolved.fixed_scale != candidate->fixed_scale) {
    state->resolved = *candidate;
    ++state->revision;
  }
  state->camera_ev = camera_ev;
  state->last_error.reset();
  return *state;
}

auto PostProcessService::CaptureViewExposureSettings(const ViewId view_id,
  const CompositionView::ViewStateHandle handle,
  const scene::ExposureSettings& requested,
  const std::optional<float> camera_ev, const bool suppress_transitions,
  const observer_ptr<const scene::Scene> world) -> const ExposureSettingsState&
{
  const auto key = ExposureCaptureKey { view_id, handle };
  if (const auto found = captured_exposure_settings_.find(key);
    found != captured_exposure_settings_.end()) {
    CHECK_F(found->second.handle == handle,
      "A captured view cannot change its exposure lifetime within a frame");
    return found->second.settings;
  }
  renderer_.ObserveExposureWorld(handle, world.get());
  auto settings = ResolveViewExposureSettings(handle, requested, camera_ev);
  const auto owner = renderer_.GetExposureSourceIntent(view_id);
  if (owner && owner->handle == handle && owner->source_loss) {
    PreserveRemovedExposureSource(owner->source_loss, handle);
  }
  renderer_.PrepareExposureTransition(handle,
    settings.resolved.authored.enabled
        && settings.resolved.authored.mode == engine::ExposureMode::kAuto
      ? ExposureTransitionPolicy::kRemeter
      : ExposureTransitionPolicy::kPreserve,
    current_sequence_, suppress_transitions || (owner && owner->diagnostic));
  std::ignore = renderer_.CaptureExposureTransition(handle, current_sequence_);
  const auto discontinuities
    = renderer_.CapturedViewDiscontinuities(handle, current_sequence_);
  if ((discontinuities
        & (1U << static_cast<unsigned>(ViewDiscontinuity::kDeviceRecovery)))
    != 0U) {
    reusable_exposure_status_.erase(handle);
  }
  return captured_exposure_settings_
    .emplace(key,
      CapturedExposureSettings {
        .handle = handle,
        .settings = std::move(settings),
      })
    .first->second.settings;
}

auto PostProcessService::CaptureSharedExposureSource(const RenderContext& ctx,
  const ViewId source_view_id,
  const CompositionView::ViewStateHandle source_handle)
  -> const postprocess::ExposurePass::Source&
{
  CaptureRegisteredExposureControls(ctx);
  if (const auto found = captured_exposure_sources_.find(source_view_id);
    found != captured_exposure_sources_.end()) {
    CHECK_F(found->second.handle == source_handle);
    return found->second;
  }
  const auto key = ExposureCaptureKey { source_view_id, source_handle };
  if (!captured_exposure_settings_.contains(key)) {
    const auto intent = renderer_.GetExposureSourceIntent(source_view_id);
    CHECK_F(intent && intent->handle == source_handle,
      "A shared source must identify its registered persistent root");
    auto requested = scene::ExposureSettings {};
    if (intent->settings) {
      requested = *intent->settings;
    } else if (const auto scene = ctx.GetScene();
      scene && scene->GetEnvironment()) {
      if (const auto post = scene->GetEnvironment()
            ->TryGetSystem<scene::environment::PostProcessVolume>()) {
        requested = post->GetExposureSettings();
      }
    }
    std::ignore = CaptureViewExposureSettings(source_view_id, source_handle,
      requested, intent->camera_ev, intent->diagnostic, ctx.GetScene());
  }
  const auto& captured = captured_exposure_settings_.at(key);
  CHECK_F(captured.handle == source_handle);
  auto config = ResolvedPostProcessConfig(PostProcessConfig {},
    captured.settings.resolved, captured.settings.revision,
    captured.settings.camera_ev);
  return captured_exposure_sources_
    .emplace(source_view_id,
      postprocess::ExposurePass::Source {
        .handle = source_handle,
        .config = std::move(config),
        .transition
        = renderer_.CaptureExposureTransition(source_handle, current_sequence_),
        .rejection
        = renderer_.CapturedExposureRejection(source_handle, current_sequence_),
        .lifetime = captured.settings.lifetime,
      })
    .first->second;
}

auto PostProcessService::CaptureRegisteredExposureControls(
  const RenderContext& ctx) -> void
{
  if (captured_control_frame_ == current_sequence_) {
    return;
  }
  captured_control_frame_ = current_sequence_;
  auto inherited = scene::ExposureSettings {};
  if (const auto scene = ctx.GetScene(); scene && scene->GetEnvironment()) {
    if (const auto post = scene->GetEnvironment()
          ->TryGetSystem<scene::environment::PostProcessVolume>()) {
      inherited = post->GetExposureSettings();
    }
  }
  for (const auto& intent : renderer_.GetRegisteredExposureIntents()) {
    if (intent.source_loss) {
      PreserveRemovedExposureSource(intent.source_loss, intent.handle);
    }
    // Registered intents already resolve each view's overrides against the
    // renderer defaults. The current scope may belong to a different view.
    const bool suppressed = intent.diagnostic;
    const auto& captured = CaptureViewExposureSettings(intent.view_id,
      intent.handle, intent.settings.value_or(inherited), intent.camera_ev,
      suppressed, ctx.GetScene());
    const auto token
      = renderer_.CaptureExposureTransition(intent.handle, current_sequence_);
    if (!token || suppressed
      || renderer_.CapturedExposureRejection(
        intent.handle, current_sequence_)) {
      continue;
    }
    std::optional<ExposureTransitionError> error;
    if (intent.owner != intent.handle) {
      error = ExposureTransitionError::kSharedConsumer;
    } else if (token->policy != ExposureTransitionPolicy::kPreserve
      && (!captured.resolved.authored.enabled
        || captured.resolved.authored.mode != engine::ExposureMode::kAuto)) {
      error = ExposureTransitionError::kNotAuto;
    } else if (token->policy == ExposureTransitionPolicy::kSeedFromEv100
      && (!token->seed_ev
        || !scene::ResolveExposureSeedLogGain(
          captured.resolved, *token->seed_ev))) {
      error = ExposureTransitionError::kUnsupportedSeed;
    }
    if (error) {
      renderer_.RejectUnsubmittedExposureTransition(*token, *error);
    }
  }
}

auto PostProcessService::BuildBindings(const Inputs& inputs) const
  -> PostProcessFrameBindings
{
  return BuildBindings(inputs, resolved_config_);
}

auto PostProcessService::BuildBindings(const Inputs& inputs,
  const ResolvedPostProcessConfig& config) const -> PostProcessFrameBindings
{
  return {
    .resolved_scene_color_srv = inputs.scene_signal_srv,
    .scene_depth_srv = inputs.scene_depth_srv,
    .scene_velocity_srv = inputs.scene_velocity_srv,
    .bloom_texture_srv = inputs.bloom_texture_srv,
    .eye_adaptation_srv = inputs.eye_adaptation_srv,
    .eye_adaptation_uav = inputs.eye_adaptation_uav,
    // TODO(exposure, temporal color): before activating this unused handoff,
    // retain its stored P/generation, convert RGB by P_current/P_stored, and
    // include its error in admission. Scope:
    // design/vortex/lld/post-process-service.md, post chain; feature
    // dependency: https://github.com/abdes/DroidNet/issues/15
    .post_history_srv = inputs.post_history_srv,
    .tone_mapper = config.Settings().tone_mapper,
    .metering_mode = config.Exposure().authored.metering_mode,
    .enable_bloom = config.Settings().enable_bloom ? 1U : 0U,
    .enable_auto_exposure
    = (config.Exposure().authored.enabled
        && config.Exposure().authored.mode == engine::ExposureMode::kAuto)
      ? 1U
      : 0U,
    .fixed_exposure = config.Exposure().fixed_scale,
    .gamma = config.Settings().gamma,
    .bloom_intensity = config.Settings().bloom_intensity,
    .bloom_threshold = config.Settings().bloom_threshold,
    .auto_exposure_speed_up = config.Exposure().authored.speed_up,
    .auto_exposure_speed_down = config.Exposure().authored.speed_down,
    .auto_exposure_low_percentile = config.Exposure().authored.low_percentile,
    .auto_exposure_high_percentile = config.Exposure().authored.high_percentile,
    .auto_exposure_min_ev = config.Exposure().authored.min_ev,
    .auto_exposure_max_ev = config.Exposure().authored.max_ev,
    .auto_exposure_min_log_luminance
    = config.Exposure().authored.min_log_luminance,
    .auto_exposure_log_luminance_range
    = config.Exposure().authored.log_luminance_range,
    .auto_exposure_target_luminance
    = config.Exposure().authored.target_luminance,
    .auto_exposure_spot_meter_radius
    = config.Exposure().authored.spot_meter_radius,
    .scene_fallback_srv = inputs.scene_fallback_srv,
    .conversion_report_srv = inputs.checked_resolution
      ? inputs.checked_resolution->conversion_srv
      : kInvalidShaderVisibleIndex,
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

auto PostProcessService::CaptureConfiguredExposure(
  const ViewId view_id, RenderContext& ctx) -> const ExposureSettingsState&
{
  CaptureRegisteredExposureControls(ctx);
  return CaptureViewExposureSettings(view_id,
    ctx.current_view.view_state_handle, GetConfig().exposure,
    resolved_config_.CameraEv(),
    GetConfig().temporary_unit_exposure
      || ctx.shader_debug_mode != ShaderDebugMode::kDisabled
      || ctx.render_mode == RenderMode::kWireframe,
    ctx.GetScene());
}

auto PostProcessService::PrepareFrameExposure(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const bool use_fp32,
  postprocess::ExposurePass::StateLease qualified_candidate,
  const bool preserve_fp32_candidate_p) -> postprocess::ExposurePass::FrameLease
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.PrepareFrameExposure",
    profiling::ProfileCategory::kPass);
  const auto& settings
    = CaptureConfiguredExposure(ctx.current_view.view_id, ctx);
  auto config = ResolvedPostProcessConfig(
    GetConfig(), settings.resolved, settings.revision, settings.camera_ev)
                  .WithDiagnosticOverride(GetConfig().temporary_unit_exposure
                    || ctx.shader_debug_mode != ShaderDebugMode::kDisabled
                    || ctx.render_mode == RenderMode::kWireframe);
  const auto source_handle = ctx.current_view.exposure_view_state_handle;
  const auto* source = !config.Settings().temporary_unit_exposure
      && source_handle != CompositionView::kInvalidViewStateHandle
      && source_handle != ctx.current_view.view_state_handle
    ? &CaptureSharedExposureSource(
        ctx, ctx.current_view.exposure_view_id, source_handle)
    : nullptr;
  return exposure_pass_->ResolveFrame(ctx, recorder, config,
    {
      .use_fp32 = use_fp32,
      .fp32_only = ctx.current_view.hdr_fp32_only,
      .preserve_fp32_candidate_p = preserve_fp32_candidate_p,
      .qualified_candidate = std::move(qualified_candidate),
      .source = source,
      .transition = renderer_.CaptureExposureTransition(
        ctx.current_view.view_state_handle, ctx.frame_sequence),
      .rejection = renderer_.CapturedExposureRejection(
        ctx.current_view.view_state_handle, ctx.frame_sequence),
      .lifetime = settings.lifetime,
    });
}

auto PostProcessService::CapturePreEnvironmentRange(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const graphics::Texture& source,
  const ShaderVisibleIndex source_srv) -> bool
{
  return ctx.current_view.frame_exposure
    && exposure_pass_->CapturePreEnvironmentRange(
      ctx, recorder, ctx.current_view.frame_exposure, source, source_srv);
}

auto PostProcessService::CheckSceneColorRange(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const graphics::Texture& source,
  const ShaderVisibleIndex source_srv) -> bool
{
  const bool recorded = ctx.current_view.frame_exposure
    && exposure_pass_->CheckSceneColorRange(
      ctx, recorder, ctx.current_view.frame_exposure, source, source_srv);
  if (!recorded) {
    InvalidatePrecision(ctx.current_view.view_state_handle);
  } else if (const auto precision
    = precision_states_.find(ctx.current_view.view_state_handle);
    precision != precision_states_.end() && !precision->second.fp32_only) {
    recorder.OnSubmission([this, handle = ctx.current_view.view_state_handle,
                            epoch = precision->second.epoch](
                            const graphics::SubmissionOutcome outcome) -> void {
      const auto current = precision_states_.find(handle);
      if (outcome == graphics::SubmissionOutcome::kDiscarded
        && current != precision_states_.end()
        && current->second.epoch == epoch) {
        InvalidatePrecision(handle);
      }
    });
  }
  return recorded;
}

auto PostProcessService::PrepareSceneExposure(const ViewId view_id,
  RenderContext& ctx, graphics::CommandRecorder& recorder, const Inputs& inputs)
  -> std::optional<PreparedExposure>
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.PrepareSceneExposure",
    profiling::ProfileCategory::kPass);
  const auto* settings = &CaptureConfiguredExposure(view_id, ctx);
  auto effective_config = ResolvedPostProcessConfig(
    GetConfig(), settings->resolved, settings->revision, settings->camera_ev);
  auto mask = settings ? settings->mask : nullptr;
  const bool initial_mask_unavailable = (settings != nullptr)
    && settings->revision == 0U
    && (settings->mask_status == ExposureMaskStatus::kPending
      || settings->mask_status == ExposureMaskStatus::kFailed);
  const bool requested_mask_missing
    = effective_config.Exposure().authored.metering_mask.get() != 0U && !mask;
  if (mask) {
    CHECK_LT_F(current_slot_.get(), frame_masks_.size());
    frame_masks_.at(current_slot_.get()).push_back(mask);
  }
  effective_config = effective_config.WithDiagnosticOverride(
    GetConfig().temporary_unit_exposure
    || ctx.shader_debug_mode != ShaderDebugMode::kDisabled
    || ctx.render_mode == RenderMode::kWireframe);
  CaptureRegisteredExposureControls(ctx);
  const auto transition = renderer_.CaptureExposureTransition(
    ctx.current_view.view_state_handle, ctx.frame_sequence);
  const auto source_handle = ctx.current_view.exposure_view_state_handle;
  const auto* source = !effective_config.Settings().temporary_unit_exposure
      && source_handle != CompositionView::kInvalidViewStateHandle
      && source_handle != ctx.current_view.view_state_handle
    ? &CaptureSharedExposureSource(
        ctx, ctx.current_view.exposure_view_id, source_handle)
    : nullptr;
  const auto exposure = exposure_pass_->Execute(ctx, recorder, effective_config,
    postprocess::ExposurePass::Inputs {
      .scene_signal = inputs.scene_signal,
      .scene_signal_srv = inputs.scene_signal_srv,
      .metering_mask = mask ? mask->texture.get() : nullptr,
      .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex,
      .metering_available
      = !initial_mask_unavailable && !requested_mask_missing,
      .require_scene_range = inputs.require_scene_range,
      .transition = !effective_config.Settings().temporary_unit_exposure
        ? transition
        : std::nullopt,
      .source = source,
      .rejection = !effective_config.Settings().temporary_unit_exposure
        ? renderer_.CapturedExposureRejection(
            ctx.current_view.view_state_handle, ctx.frame_sequence)
        : std::nullopt,
      .lifetime = settings->lifetime,
      .scene_composition = {},
    });
  if (exposure.solve_failed || (exposure.frame && !exposure.state)) {
    InvalidatePrecision(ctx.current_view.view_state_handle);
  }
  if (exposure.frame && !exposure.state) {
    last_execution_state_ = { .tonemap_requested = true, .view_id = view_id };
    return std::nullopt;
  }
  auto status_transition = std::optional<ExposureTransitionToken> {};
  const auto precision
    = precision_states_.find(ctx.current_view.view_state_handle);
  const auto precision_epoch = precision != precision_states_.end()
      && precision->second.configured_frame == ctx.frame_sequence
      && !precision->second.fp32_only
    ? std::optional { precision->second.epoch }
    : std::nullopt;
  if (transition && exposure.state && !exposure.solve_failed
    && !effective_config.Settings().temporary_unit_exposure) {
    if (precision_epoch) {
      status_transition = transition;
    } else {
      EnqueueExposureStatus(recorder, *transition, exposure.state,
        ctx.frame_sequence, settings->revision);
    }
  }
  return PreparedExposure {
    .owner = this,
    .exposure = exposure,
    .config = std::move(effective_config),
    .view_id = view_id,
    .handle = ctx.current_view.view_state_handle,
    .lifetime = settings->lifetime,
    .sequence = ctx.frame_sequence,
    .status_transition = status_transition,
    .precision_epoch = precision_epoch,
  };
}

auto PostProcessService::ValidatePreparedExposure(const ViewId view_id,
  const RenderContext& ctx, const PreparedExposure& prepared) const -> void
{
  CHECK_F(prepared.owner == this && prepared.view_id == view_id
      && prepared.handle == ctx.current_view.view_state_handle
      && prepared.lifetime
        == renderer_.EnsureExposureLifetime(ctx.current_view.view_state_handle)
      && prepared.sequence == ctx.frame_sequence,
    "Prepared exposure belongs to another service, view lifetime or frame");
}

auto PostProcessService::ConvertSceneColor(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const PreparedExposure& prepared,
  const Inputs& inputs, graphics::Texture& destination,
  const ShaderVisibleIndex destination_uav) -> bool
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.ConvertSceneColor", profiling::ProfileCategory::kPass);
  ValidatePreparedExposure(ctx.current_view.view_id, ctx, prepared);
  CHECK_NOTNULL_F(prepared.exposure.frame.get());
  CHECK_F(!published_views_.contains(prepared.view_id),
    "Checked conversion must precede post-process publication");
  const auto precision = precision_states_.find(prepared.handle);
  CHECK_F(precision == precision_states_.end()
      || precision->second.finalized_frame != ctx.frame_sequence,
    "Checked conversion must precede precision finalization");
  const auto& settings
    = captured_exposure_settings_.at({ prepared.view_id, prepared.handle })
        .settings;
  const auto& mask = settings.mask;
  const auto& authored = settings.resolved.authored;
  const bool needs_mask = authored.enabled
    && authored.mode == engine::ExposureMode::kAuto
    && authored.min_ev != authored.max_ev && authored.metering_mask.get() != 0U;
  const bool initial_mask_unavailable = settings.revision == 0U
    && (settings.mask_status == ExposureMaskStatus::kPending
      || settings.mask_status == ExposureMaskStatus::kFailed);
  if (initial_mask_unavailable || (needs_mask && !mask)) {
    InvalidatePrecision(prepared.handle);
    return false;
  }
  std::uint32_t composition_products = 0U;
  if (prepared.precision_epoch) {
    if (precision == precision_states_.end()
      || precision->second.epoch != *prepared.precision_epoch
      || precision->second.configured_frame != ctx.frame_sequence) {
      return false;
    }
    composition_products = precision->second.expected_products;
  }
  const bool submitted = exposure_pass_->ConvertCheckedSceneColor(ctx, recorder,
    prepared.exposure.frame, prepared.config,
    {
      .scene_signal = inputs.scene_signal,
      .scene_signal_srv = inputs.scene_signal_srv,
      .metering_mask = mask ? mask->texture.get() : nullptr,
      .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex,
      .transition = {},
      .rejection = {},
      .composition_products = composition_products,
      .scene_composition = {},
    },
    destination, destination_uav);
  if (!submitted) {
    InvalidatePrecision(prepared.handle);
  }
  return submitted;
}

auto PostProcessService::CurrentExposureGeneration(
  const CompositionView::ViewStateHandle handle,
  const std::uint64_t lifetime) const -> std::uint64_t
{
  const auto status = renderer_.InspectExposureTransition(handle);
  return status && status->request.lifetime == lifetime
    ? status->request.generation
    : 0U;
}

auto PostProcessService::InvalidatePrecision(
  const CompositionView::ViewStateHandle handle) -> void
{
  const auto found = precision_states_.find(handle);
  if (found == precision_states_.end()) {
    return;
  }
  auto& precision = found->second;
  CHECK_NE_F(precision.epoch, (std::numeric_limits<std::uint64_t>::max)());
  ++precision.epoch;
  precision.candidate.reset();
  precision.restart_streak = true;
  precision.prepared_frame.reset();
  precision.finalized_frame.reset();
}

auto PostProcessService::SelectPrecisionCandidate(RenderContext& ctx,
  const postprocess::ExposurePass::EligibilityInputs& requirements)
  -> postprocess::ExposurePass::StateLease
{
  CHECK_F(requirements.product_layout_revision != 0U
    && requirements.expected_products != 0U);
  const auto handle = ctx.current_view.view_state_handle;
  if (handle == CompositionView::kInvalidViewStateHandle) {
    return {};
  }
  const auto& settings
    = CaptureConfiguredExposure(ctx.current_view.view_id, ctx);
  const auto generation = CurrentExposureGeneration(handle, settings.lifetime);
  const bool diagnostic = GetConfig().temporary_unit_exposure
    || ctx.shader_debug_mode != ShaderDebugMode::kDisabled
    || ctx.render_mode == RenderMode::kWireframe;
  const auto source_handle = ctx.current_view.exposure_view_state_handle;
  const auto* source = !diagnostic
      && source_handle != CompositionView::kInvalidViewStateHandle
      && source_handle != handle
    ? &CaptureSharedExposureSource(
        ctx, ctx.current_view.exposure_view_id, source_handle)
    : nullptr;
  const auto source_identity
    = source ? source->handle : CompositionView::kInvalidViewStateHandle;
  const auto source_lifetime = source ? source->lifetime : 0U;
  const auto source_revision = source ? source->config.Revision() : 0U;
  const auto source_generation
    = source && source->transition ? source->transition->generation : 0U;
  const auto source_transition = source
    ? renderer_.InspectExposureTransition(source->handle)
    : std::nullopt;
  const bool source_pending = source_transition
    && source_transition->phase == ExposureTransitionPhase::kQueued
    && source_transition->applied_generation
      < source_transition->request.generation;
  auto& precision = precision_states_[handle];
  const bool changed = precision.lifetime != settings.lifetime
    || precision.settings_revision != settings.revision
    || precision.layout_revision != requirements.product_layout_revision
    || precision.expected_products != requirements.expected_products
    || precision.transition_generation != generation
    || precision.source_handle != source_identity
    || precision.source_lifetime != source_lifetime
    || precision.source_revision != source_revision
    || precision.source_generation != source_generation
    || precision.source_pending != source_pending
    || precision.diagnostic != diagnostic
    || precision.fp32_only != ctx.current_view.hdr_fp32_only;
  if (changed
    || (requirements.invalidate_previous
      && (!precision.restart_streak
        || precision.configured_frame != ctx.frame_sequence))) {
    CHECK_NE_F(precision.epoch, (std::numeric_limits<std::uint64_t>::max)());
    ++precision.epoch;
    precision.lifetime = settings.lifetime;
    precision.settings_revision = settings.revision;
    precision.layout_revision = requirements.product_layout_revision;
    precision.expected_products = requirements.expected_products;
    precision.transition_generation = generation;
    precision.source_handle = source_identity;
    precision.source_lifetime = source_lifetime;
    precision.source_revision = source_revision;
    precision.source_generation = source_generation;
    precision.source_pending = source_pending;
    precision.diagnostic = diagnostic;
    precision.fp32_only = ctx.current_view.hdr_fp32_only;
    precision.restart_streak = true;
    precision.last_completed_frame = 0U;
    precision.candidate.reset();
  }
  precision.configured_frame = ctx.frame_sequence;
  if (diagnostic || precision.fp32_only) {
    return {};
  }
  if (source_pending) {
    return {};
  }
  const auto transition = renderer_.InspectExposureTransition(handle);
  if (transition && transition->phase == ExposureTransitionPhase::kQueued
    && transition->applied_generation < transition->request.generation) {
    return {};
  }
  return precision.candidate;
}

auto PostProcessService::PrepareScenePrecision(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const PreparedExposure& prepared,
  const std::span<const postprocess::ExposurePass::HdrProduct> products,
  std::optional<postprocess::ExposurePass::SceneComposition> composition)
  -> bool
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.PrepareScenePrecision",
    profiling::ProfileCategory::kPass);
  ValidatePreparedExposure(ctx.current_view.view_id, ctx, prepared);
  // Preparation already invalidated this failed attempt. Repeated consumers
  // of its fallback must not invalidate a later successful same-frame retry.
  if (prepared.exposure.solve_failed || ctx.current_view.hdr_fp32_only) {
    return false;
  }
  const auto found = precision_states_.find(prepared.handle);
  if (found == precision_states_.end() || !prepared.precision_epoch
    || *prepared.precision_epoch != found->second.epoch
    || found->second.configured_frame != ctx.frame_sequence) {
    return false;
  }
  auto& precision = found->second;
  if (precision.prepared_frame == ctx.frame_sequence) {
    return precision.prepared_epoch == precision.epoch;
  }
  CHECK_F(!published_views_.contains(prepared.view_id),
    "Precision preparation must precede post-process publication");
  const auto reject = [&] -> bool {
    // An earlier deferred/completed result cannot authorize a later failed
    // frame. Preserve exposure events while invalidating only qualification.
    InvalidatePrecision(prepared.handle);
    return false;
  };
  if (precision.diagnostic || !prepared.exposure.frame
    || !prepared.exposure.state
    || CurrentExposureGeneration(prepared.handle, prepared.lifetime)
      != precision.transition_generation) {
    return reject();
  }
  const auto& settings
    = captured_exposure_settings_.at({ prepared.view_id, prepared.handle })
        .settings;
  const auto& mask = settings.mask;
  if (settings.revision == 0U
    && (settings.mask_status == ExposureMaskStatus::kPending
      || settings.mask_status == ExposureMaskStatus::kFailed)) {
    return reject();
  }
  for (const auto& product : products) {
    if (product.id == postprocess::ExposurePass::HdrProduct::kSkyView
      || product.id
        == postprocess::ExposurePass::HdrProduct::kCameraAerialPerspective
      || product.id == postprocess::ExposurePass::HdrProduct::kVolumetricFog) {
      std::ignore = exposure_pass_->GatherFilterGradients(
        ctx, recorder, prepared.exposure.frame, product);
    }
  }
  for (const auto& product : products) {
    if (product.id
        == postprocess::ExposurePass::HdrProduct::kCameraAerialPerspective
      && product.texture != nullptr) {
      // Intermediate retained-error transport only. It cannot authorize
      // admission until the remaining consumer/candidate terms are qualified.
      std::ignore = exposure_pass_->PropagateOpaqueApError(
        ctx, recorder, prepared.exposure.frame, product.consumer_rgb_gain);
      break;
    }
  }
  if (!exposure_pass_->EvaluateFp16Products(ctx, recorder,
        prepared.exposure.frame, prepared.config, products,
        {
          .metering_mask = mask ? mask->texture.get() : nullptr,
          .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex,
          .transition = {},
          .rejection = {},
          .scene_composition = composition,
        })) {
    return reject();
  }
  precision.prepared_frame = ctx.frame_sequence;
  precision.prepared_epoch = precision.epoch;
  recorder.OnSubmission(
    [this, handle = prepared.handle, sequence = ctx.frame_sequence,
      epoch = precision.epoch](
      const graphics::SubmissionOutcome outcome) -> void {
      if (outcome != graphics::SubmissionOutcome::kDiscarded) {
        return;
      }
      const auto current = precision_states_.find(handle);
      if (current == precision_states_.end() || current->second.epoch != epoch
        || current->second.prepared_frame != sequence) {
        return;
      }
      current->second.prepared_frame.reset();
      current->second.finalized_frame.reset();
      InvalidatePrecision(handle);
    });
  return true;
}

auto PostProcessService::FinalizeScenePrecision(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const PreparedExposure& prepared) -> bool
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.FinalizeScenePrecision",
    profiling::ProfileCategory::kPass);
  ValidatePreparedExposure(ctx.current_view.view_id, ctx, prepared);
  if (prepared.exposure.solve_failed) {
    return false;
  }
  const auto found = precision_states_.find(prepared.handle);
  if (found == precision_states_.end() || !prepared.precision_epoch
    || *prepared.precision_epoch != found->second.epoch
    || found->second.configured_frame != ctx.frame_sequence) {
    return false;
  }
  auto& precision = found->second;
  if (precision.finalized_frame == ctx.frame_sequence) {
    return precision.finalized_epoch == precision.epoch;
  }
  CHECK_F(!published_views_.contains(prepared.view_id),
    "Precision finalization must precede post-process publication");
  if (precision.prepared_frame != ctx.frame_sequence
    || precision.prepared_epoch != precision.epoch
    || CurrentExposureGeneration(prepared.handle, prepared.lifetime)
      != precision.transition_generation
    || !exposure_pass_->FinalizeFp16Suitability(ctx, recorder,
      prepared.exposure.frame,
      {
        .product_layout_revision = precision.layout_revision,
        .expected_products = precision.expected_products,
        .invalidate_previous = precision.restart_streak,
      })) {
    InvalidatePrecision(prepared.handle);
    return false;
  }
  precision.finalized_frame = ctx.frame_sequence;
  precision.finalized_epoch = precision.epoch;
  recorder.OnSubmission([this, handle = prepared.handle,
                          epoch = precision.epoch](
                          const graphics::SubmissionOutcome outcome) -> void {
    const auto current = precision_states_.find(handle);
    if (outcome == graphics::SubmissionOutcome::kSubmitted
      && current != precision_states_.end() && current->second.epoch == epoch) {
      current->second.restart_streak = false;
    }
  });
  PublishExposureStatusOnSubmission(recorder,
    PendingExposureStatus { .readback_graphics = {},
      .state = prepared.exposure.state,
      .readback = {},
      .reuse_pool = {},
      .token = prepared.status_transition,
      .handle = prepared.handle,
      .lifetime = prepared.lifetime,
      .frame_sequence = ctx.frame_sequence.get(),
      .settings_revision = prepared.config.Revision(),
      .precision_epoch = {},
      .precision = PrecisionTicket { .layout_revision=precision.layout_revision,
        .transition_generation=precision.transition_generation,
        .normal_mode=ctx.current_view.hdr_color_format == Format::kRGBA16Float,
        .auto_owner=prepared.config.Exposure().authored.enabled
          && prepared.config.Exposure().authored.mode
            == engine::ExposureMode::kAuto
          && precision.source_handle
            == CompositionView::kInvalidViewStateHandle, }, });
  return true;
}

auto PostProcessService::Record(const ViewId view_id, RenderContext& ctx,
  graphics::CommandRecorder& recorder, const Inputs& inputs,
  const PreparedExposure* prepared_exposure) -> bool
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.Execute", profiling::ProfileCategory::kPass);
  auto local_exposure = std::optional<PreparedExposure> {};
  if (!prepared_exposure) {
    local_exposure = PrepareSceneExposure(view_id, ctx, recorder, inputs);
    if (!local_exposure) {
      last_execution_state_ = { .tonemap_requested = true, .view_id = view_id };
      return false;
    }
    prepared_exposure = &*local_exposure;
  }
  ValidatePreparedExposure(view_id, ctx, *prepared_exposure);
  const auto& exposure = prepared_exposure->exposure;
  const auto& effective_config = prepared_exposure->config;
  CHECK_F(
    !inputs.checked_resolution || inputs.checked_resolution == exposure.frame,
    "Checked color report must belong to the prepared exposure frame");
  auto bindings = BuildBindings(inputs, effective_config);
  if (effective_config.Settings().temporary_unit_exposure) {
    bindings.enable_auto_exposure = 0U;
    bindings.fixed_exposure = 1.0F;
  }
  bindings.eye_adaptation_srv = exposure.exposure_buffer_srv;
  bindings.eye_adaptation_uav = exposure.exposure_buffer_uav;
  const auto slot = PublishBindings(view_id, bindings);
  const auto bloom = bloom_pass_->Execute(effective_config, bindings);
  const auto tonemap = tonemap_pass_->Record(ctx, recorder,
    postprocess::TonemapPass::Inputs {
      .scene_signal = inputs.scene_signal,
      .exposure_buffer = exposure.exposure_buffer,
      .frame_exposure_buffer
      = exposure.frame ? exposure.frame->buffer.get() : nullptr,
      .scene_signal_srv = inputs.scene_signal_srv,
      .bloom_texture_srv = bloom.bloom_texture_srv,
      .exposure_buffer_srv = exposure.exposure_buffer_srv,
      .frame_exposure_srv
      = exposure.frame ? exposure.frame->srv_index : kInvalidShaderVisibleIndex,
      .post_target = inputs.post_target,
      .tone_mapper = effective_config.Settings().tone_mapper,
      .exposure_value = exposure.exposure_value,
      .gamma = effective_config.Settings().gamma,
      .bloom_intensity = effective_config.Settings().bloom_intensity,
      .background_color = environment::ResolveSceneBackground(ctx),
      .scene_fallback = inputs.scene_fallback,
      .scene_fallback_srv = inputs.scene_fallback_srv,
      .conversion_report = inputs.checked_resolution
        ? inputs.checked_resolution->conversion_buffer.get()
        : nullptr,
      .conversion_report_srv = inputs.checked_resolution
        ? inputs.checked_resolution->conversion_srv
        : kInvalidShaderVisibleIndex,
    });

  if (prepared_exposure->status_transition) {
    EnqueueExposureStatus(recorder, *prepared_exposure->status_transition,
      exposure.state, ctx.frame_sequence, effective_config.Revision());
  }

  const auto submitted_state = ExecutionState {
    .published_bindings = slot != kInvalidShaderVisibleIndex,
    .tonemap_requested = tonemap.requested,
    .tonemap_executed = tonemap.recorded,
    .wrote_visible_output = tonemap.writes_visible_output,
    .bloom_requested = bloom.requested,
    .bloom_executed = bloom.executed,
    .auto_exposure_requested = (effective_config.Exposure().authored.enabled
                                 && effective_config.Exposure().authored.mode
                                   == engine::ExposureMode::kAuto)
      && !effective_config.Settings().temporary_unit_exposure
      && !exposure.borrowed_exposure && exposure.requested,
    .auto_exposure_executed = (effective_config.Exposure().authored.enabled
                                && effective_config.Exposure().authored.mode
                                  == engine::ExposureMode::kAuto)
      && !effective_config.Settings().temporary_unit_exposure
      && !exposure.borrowed_exposure && exposure.executed,
    .used_fixed_exposure = exposure.used_fixed_exposure,
    .view_id = view_id,
    .post_process_frame_slot = slot,
    .exposure_value = exposure.exposure_value,
  };
  last_execution_state_ = { .tonemap_requested = true, .view_id = view_id };
  recorder.OnSubmission(
    [this, submitted_state](const graphics::SubmissionOutcome outcome) -> void {
      if (outcome == graphics::SubmissionOutcome::kSubmitted) {
        last_execution_state_ = submitted_state;
      } else {
        published_views_.erase(submitted_state.view_id);
      }
    });
  return tonemap.writes_visible_output;
}

auto PostProcessService::PreserveRemovedExposureSource(
  std::shared_ptr<const ExposureSourceLoss> loss,
  CompositionView::ViewStateHandle only_consumer) -> void
{
  CHECK_NOTNULL_F(loss.get());
  auto source = postprocess::ExposurePass::Source {
    .handle = loss->source_handle,
    .config = {},
    .transition = loss->transition,
    .rejection = loss->rejection,
    .lifetime = loss->source_lifetime,
  };
  if (const auto captured
    = captured_exposure_sources_.find(loss->source_view_id);
    captured != captured_exposure_sources_.end()
    && captured->second.lifetime == loss->source_lifetime) {
    source = captured->second;
  } else if (const auto accepted = exposure_settings_.find(loss->source_handle);
    accepted != exposure_settings_.end()
    && accepted->second.lifetime == loss->source_lifetime) {
    source.config = ResolvedPostProcessConfig(PostProcessConfig {},
      accepted->second.resolved, accepted->second.revision,
      accepted->second.camera_ev);
  } else {
    auto requested = loss->settings;
    // No accepted mask revision exists for a never-prepared source.
    if (requested.enabled && requested.mode == engine::ExposureMode::kAuto
      && requested.min_ev != requested.max_ev
      && requested.metering_mask.get() != 0U) {
      requested = scene::ExposureSettings {};
    }
    auto resolved = scene::ResolveExposureSettings(requested, loss->camera_ev);
    if (!resolved) {
      resolved = scene::ResolveExposureSettings(scene::ExposureSettings {});
    }
    source.config = ResolvedPostProcessConfig(
      PostProcessConfig {}, *resolved, 0U, loss->camera_ev);
  }
  exposure_pass_->PreserveRemovedSource(std::move(loss), source, only_consumer);
}

auto PostProcessService::RemoveViewState(const ViewId view_id,
  const CompositionView::ViewStateHandle view_state_handle) -> void
{
  published_views_.erase(view_id);
  captured_exposure_settings_.erase({ view_id, view_state_handle });
  exposure_settings_.erase(view_state_handle);
  precision_states_.erase(view_state_handle);
  pending_exposure_status_.erase(view_state_handle);
  deferred_exposure_status_.erase(view_state_handle);
  reusable_exposure_status_.erase(view_state_handle);
  renderer_.RetireExposureTransitions(view_state_handle);
  exposure_pass_->RemoveViewState(view_state_handle);
}

void PostProcessService::EnqueueExposureStatus(
  graphics::CommandRecorder& recorder, const ExposureTransitionToken& token,
  postprocess::ExposurePass::StateLease state,
  const frame::SequenceNumber sequence, const std::uint64_t settings_revision)
{
  PublishExposureStatusOnSubmission(recorder,
    PendingExposureStatus {
      .readback_graphics = {},
      .state = std::move(state),
      .readback = {},
      .reuse_pool = {},
      .token = token,
      .handle = token.target,
      .lifetime = token.lifetime,
      .frame_sequence = sequence.get(),
      .settings_revision = settings_revision,
      .precision_epoch = {},
      .precision = {},
    });
}

void PostProcessService::PublishExposureStatusOnSubmission(
  graphics::CommandRecorder& recorder, PendingExposureStatus job)
{
  recorder.OnSubmission(
    [this, job = std::move(job)](
      const graphics::SubmissionOutcome outcome) mutable -> void {
      if (outcome != graphics::SubmissionOutcome::kSubmitted) {
        return;
      }
      if (job.token) {
        renderer_.MarkExposureTransitionSubmitted(*job.token);
      }
      QueueExposureStatus(std::move(job));
    });
}

auto PostProcessService::QueueExposureStatus(PendingExposureStatus job) -> void
{
  job.control_revision
    = renderer_.GetDiagnosticsService().GetHdrPrecisionControlRevision();
  if (const auto precision = precision_states_.find(job.handle);
    precision != precision_states_.end()) {
    job.precision_epoch = precision->second.epoch;
  }
  if (!IsExposureStatusNeeded(job)) {
    return;
  }
  const auto same_frame = [&](const PendingExposureStatus& existing) -> bool {
    return existing.lifetime == job.lifetime
      && existing.frame_sequence == job.frame_sequence
      && existing.settings_revision == job.settings_revision
      && existing.control_revision == job.control_revision
      && existing.precision_epoch == job.precision_epoch;
  };
  if (const auto pending = pending_exposure_status_.find(job.handle);
    pending != pending_exposure_status_.end()) {
    for (const auto& existing : pending->second) {
      if (same_frame(existing)) {
        CHECK_F(!job.precision || existing.precision,
          "A precision status cannot follow an earlier copy of the same frame");
        return;
      }
    }
  }
  if (const auto deferred = deferred_exposure_status_.find(job.handle);
    deferred != deferred_exposure_status_.end()
    && same_frame(deferred->second)) {
    if (!job.precision || deferred->second.precision) {
      return;
    }
  }
  if (!TryEnqueueExposureStatus(job)) {
    DeferExposureStatus(std::move(job));
  } else if (const auto older = deferred_exposure_status_.find(job.handle);
    older != deferred_exposure_status_.end()
    && older->second.frame_sequence <= job.frame_sequence) {
    deferred_exposure_status_.erase(older);
  }
}

auto PostProcessService::IsExposureStatusNeeded(
  const PendingExposureStatus& job) const -> bool
{
  if (job.control_revision
    != renderer_.GetDiagnosticsService().GetHdrPrecisionControlRevision()) {
    return false;
  }
  // A failed qualification attempt cannot erase a valid authored solve. Its
  // epoch gates eligibility below; the control revision gates both outcomes.
  return (job.token && renderer_.NeedsExposureAcknowledgement(*job.token))
    || IsPrecisionStatusNeeded(job);
}

auto PostProcessService::IsPrecisionStatusNeeded(
  const PendingExposureStatus& job) const -> bool
{
  if (!job.precision) {
    return false;
  }
  const auto found = precision_states_.find(job.handle);
  if (found == precision_states_.end()) {
    return false;
  }
  const auto& current = found->second;
  if (const auto settings = exposure_settings_.find(job.handle);
    settings != exposure_settings_.end()
    && (settings->second.lifetime != job.lifetime
      || settings->second.revision != job.settings_revision)) {
    return false;
  }
  return current.lifetime == job.lifetime
    && renderer_.EnsureExposureLifetime(job.handle) == job.lifetime
    && current.settings_revision == job.settings_revision
    && current.layout_revision == job.precision->layout_revision
    && job.precision_epoch == current.epoch && !current.diagnostic
    && current.last_completed_frame < job.frame_sequence
    && CurrentExposureGeneration(job.handle, job.lifetime)
    == job.precision->transition_generation;
}

auto PostProcessService::DeferExposureStatus(PendingExposureStatus job) -> void
{
  if (!IsExposureStatusNeeded(job)) {
    return;
  }
  job.readback.reset();
  job.reuse_pool.reset();
  const auto found = deferred_exposure_status_.find(job.handle);
  if (found == deferred_exposure_status_.end()
    || found->second.lifetime != job.lifetime
    || found->second.frame_sequence <= job.frame_sequence) {
    deferred_exposure_status_.insert_or_assign(job.handle, std::move(job));
  }
}

auto PostProcessService::TryEnqueueExposureStatus(PendingExposureStatus job)
  -> bool
{
  using oxygen::graphics::GpuBufferReadback;
  using oxygen::graphics::GpuEventScope;
  using oxygen::graphics::QueueRole;
  using oxygen::graphics::ReadbackError;
  using oxygen::graphics::ReadbackTicket;

  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.TryEnqueueExposureStatus",
    profiling::ProfileCategory::kPass);
  auto& pending = pending_exposure_status_[job.handle];
  if (pending.size() >= frame::kFramesInFlight.get()) {
    return false;
  }
  auto gfx = renderer_.GetGraphics();
  if (!gfx) {
    return false;
  }
  auto manager = gfx->GetReadbackManager();
  if (!manager) {
    return false;
  }
  auto& pool = reusable_exposure_status_[job.handle];
  if (!pool || pool->lifetime != job.lifetime || pool->graphics_owner != gfx) {
    pool = std::make_shared<ExposureReadbackPool>();
    pool->graphics_owner = gfx;
    pool->lifetime = job.lifetime;
  }
  std::shared_ptr<GpuBufferReadback> readback;
  if (!pool->available.empty()) {
    readback = std::move(pool->available.back());
    pool->available.pop_back();
  } else {
    readback = manager->CreateBufferReadback("Exposure completed status");
  }
  if (!readback) {
    return false;
  }
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(QueueRole::kGraphics), "Exposure status readback");
  if (!recorder) {
    return false;
  }
  renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(*recorder);
  const auto recording = recorder->GetCommandListForInspection();
  const auto ticket = [&] -> std::expected<ReadbackTicket, ReadbackError> {
    GpuEventScope scope(*recorder, "Vortex.PostProcess.Exposure.StatusReadback",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kSynchronization);
    if (!recorder->AdoptKnownResourceState(*job.state->status_buffer)) {
      recorder->BeginTrackingResourceState(*job.state->status_buffer,
        graphics::ResourceStates::kCopySource, false);
    }
    return readback->EnqueueCopy(*recorder, *job.state->status_buffer,
      { 0U, sizeof(ExposureCompletedStatus) });
  }();
  std::ignore = recorder.Submit();
  if (!ticket || !recording || !recording->IsSubmitted()) {
    std::ignore = readback->Cancel();
    return false;
  }
  job.readback_graphics = std::move(gfx);
  job.readback = std::move(readback);
  job.reuse_pool = pool;
  pending.push_back(std::move(job));
  return true;
}

auto PostProcessService::RecycleExposureStatus(PendingExposureStatus& job)
  -> void
{
  const auto pool = job.reuse_pool.lock();
  if (!pool || pool->lifetime != job.lifetime
    || pool->graphics_owner != job.readback_graphics || !job.readback
    || pool->available.size() >= frame::kFramesInFlight.get()) {
    return;
  }
  // Poll's mapped guard has been destroyed before this call. Rearming never
  // waits and refuses incomplete, cancelled, failed or still-mapped requests.
  if (job.readback->ResetForReuse().has_value()) {
    pool->available.push_back(std::move(job.readback));
  }
}

auto PostProcessService::PollExposureStatus() -> void
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.PostProcess.PollExposureStatus", profiling::ProfileCategory::kPass);
  const auto integer
    = [](const std::array<std::uint32_t, 2>& words) -> std::uint64_t {
    return std::uint64_t { std::get<0>(words) }
    | (std::uint64_t { std::get<1>(words) } << 32U);
  };
  for (auto& [handle, pending] : pending_exposure_status_) {
    while (!pending.empty()) {
      auto& job = pending.front();
      const auto ready = job.readback->IsReady();
      if (ready && !*ready) {
        break;
      }
      const auto complete = [&] -> bool {
        if (!IsExposureStatusNeeded(job)) {
          return true;
        }
        if (!ready) {
          return false;
        }
        const auto mapped = job.readback->TryMap();
        if (!mapped
          || mapped->Bytes().size() < sizeof(ExposureCompletedStatus)) {
          return false;
        }
        ExposureCompletedStatus status {};
        std::memcpy(&status, mapped->Bytes().data(), sizeof(status));
        auto requested_generation
          = job.precision ? job.precision->transition_generation : 0U;
        if (!job.precision && job.token) {
          requested_generation = job.token->generation;
        }
        if (integer(status.view_state_identity) != job.lifetime
          || integer(status.frame_sequence) != job.frame_sequence
          || integer(status.settings_revision) != job.settings_revision
          || integer(status.requested_generation) != requested_generation
          || (job.precision
            && integer(status.product_layout_revision)
              != job.precision->layout_revision)) {
          return true; // A complete stale packet cannot become valid by
                       // retrying.
        }
        std::optional<ExposureTransitionError> rejection;
        if ((status.flags & 8U) != 0U) {
          const auto seed_rejection = status.transition_rejection_reason == 3U
            ? ExposureTransitionError::kSharedConsumer
            : ExposureTransitionError::kUnsupportedSeed;
          rejection = status.transition_rejection_reason == 1U
            ? ExposureTransitionError::kNotAuto
            : seed_rejection;
        }
        if (job.token) {
          renderer_.CompleteExposureTransition(
            *job.token, integer(status.applied_generation), rejection);
        }
        if (IsPrecisionStatusNeeded(job)) {
          auto& precision = precision_states_.at(job.handle);
          precision.last_completed_frame = job.frame_sequence;
          // Producer failure or rejected current conversion is distinct from
          // prospective FP16 suitability. A failed producer invalidates
          // metering; rejected conversion retains the valid FP32 meter and
          // adaptation.
          if (job.precision->normal_mode
            && (status.flags & (16U | 32U)) != 0U) {
            InvalidatePrecision(job.handle);
            if (job.precision->auto_owner && (status.flags & 16U) != 0U) {
              renderer_.RequestExposureRecovery(
                job.handle, job.lifetime, job.precision->transition_generation);
            }
            return true;
          }
          const bool eligible = (status.flags & 7U) == 5U
            && status.fp16_eligible_streak == 2U
            && integer(status.candidate_state_generation) == job.frame_sequence;
          precision.candidate = eligible ? job.state : nullptr;
        }
        return true;
      };
      if (!complete()) {
        DeferExposureStatus(std::move(job));
      } else {
        RecycleExposureStatus(job);
      }
      pending.pop_front();
    }
  }
  std::erase_if(pending_exposure_status_,
    [](const auto& entry) -> auto { return entry.second.empty(); });
  for (auto it = deferred_exposure_status_.begin();
    it != deferred_exposure_status_.end();) {
    if (!IsExposureStatusNeeded(it->second)
      || TryEnqueueExposureStatus(it->second)) {
      it = deferred_exposure_status_.erase(it);
    } else {
      ++it;
    }
  }
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
