//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstring>
#include <memory>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>

#include <Oxygen/Vortex/Environment/SceneBackground.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/PostProcess/Passes/BloomPass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex {

namespace {
  auto ApplyExposureRevision(PostProcessConfig& config,
    const PostProcessService::ExposureSettingsState& state) -> void
  {
    const auto& exposure = state.resolved.authored;
    config.resolved_exposure = state.resolved;
    config.exposure_settings_revision = state.revision;
    config.enable_auto_exposure
      = exposure.enabled && exposure.mode == engine::ExposureMode::kAuto;
    config.fixed_exposure = state.resolved.fixed_scale;
    config.metering_mode = exposure.metering_mode;
    config.auto_exposure_speed_up = exposure.speed_up;
    config.auto_exposure_speed_down = exposure.speed_down;
    config.auto_exposure_low_percentile = exposure.low_percentile;
    config.auto_exposure_high_percentile = exposure.high_percentile;
    config.auto_exposure_min_ev = exposure.min_ev;
    config.auto_exposure_max_ev = exposure.max_ev;
    config.auto_exposure_min_log_luminance = exposure.min_log_luminance;
    config.auto_exposure_log_luminance_range = exposure.log_luminance_range;
    config.auto_exposure_target_luminance = exposure.target_luminance;
    config.auto_exposure_spot_meter_radius = exposure.spot_meter_radius;
  }
} // namespace

PostProcessService::PostProcessService(
  Renderer& renderer, observer_ptr<content::IAssetLoader> asset_loader)
  : renderer_(renderer)
  , asset_loader_(asset_loader)
  , exposure_pass_(std::make_unique<postprocess::ExposurePass>(renderer))
  , bloom_pass_(std::make_unique<postprocess::BloomPass>(renderer))
  , tonemap_pass_(std::make_unique<postprocess::TonemapPass>(renderer))
{
  SetConfig(config_);
}

PostProcessService::~PostProcessService()
{
  pending_exposure_status_.clear();
  deferred_exposure_status_.clear();
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
  if (current_sequence_ == sequence && current_slot_ == slot)
    return;
  PollExposureStatus();
  captured_exposure_settings_.clear();
  captured_exposure_sources_.clear();
  current_sequence_ = sequence;
  current_slot_ = slot;
  exposure_pass_->OnFrameStart(sequence, slot);
  CHECK_LT_F(slot.get(), frame_masks_.size());
  frame_masks_[slot.get()].clear();
  if (mask_binder_) {
    mask_binder_->OnFrameStart();
  }
  published_views_.clear();
  last_execution_state_ = {};
  if (EnsurePublishResources()) {
    bindings_publisher_->OnFrameStart(sequence, slot);
  }
}

auto PostProcessService::SetConfig(const PostProcessConfig& config) -> void
{
  if (!config.resolved_exposure) {
    // Low-level pass clients use the same canonical resolver as scene views.
    auto settings = scene::ExposureSettings {};
    settings.mode = config.enable_auto_exposure ? engine::ExposureMode::kAuto
                                                : engine::ExposureMode::kManual;
    if (!std::isfinite(config.fixed_exposure)
      || config.fixed_exposure < 0x1p-32F || config.fixed_exposure > 0x1p32F) {
      LOG_F(ERROR,
        "Post-process fixed exposure is outside the supported gain domain");
      return;
    }
    settings.manual_ev = static_cast<float>(
      -std::log2(static_cast<double>(config.fixed_exposure)));
    settings.key = engine::kExposureCalibrationKey;
    settings.metering_mode = config.metering_mode;
    settings.min_ev = config.auto_exposure_min_ev;
    settings.max_ev = config.auto_exposure_max_ev;
    settings.speed_up = config.auto_exposure_speed_up;
    settings.speed_down = config.auto_exposure_speed_down;
    settings.low_percentile = config.auto_exposure_low_percentile;
    settings.high_percentile = config.auto_exposure_high_percentile;
    settings.min_log_luminance = config.auto_exposure_min_log_luminance;
    settings.log_luminance_range = config.auto_exposure_log_luminance_range;
    settings.target_luminance = config.auto_exposure_target_luminance;
    settings.spot_meter_radius = config.auto_exposure_spot_meter_radius;
    const auto resolved = scene::ResolveExposureSettings(settings);
    if (!resolved) {
      LOG_F(ERROR, "Post-process exposure config rejected: {}",
        scene::to_string(resolved.error()));
      return;
    }
    config_ = config;
    config_.resolved_exposure = *resolved;
    return;
  }
  config_ = config;
}

auto PostProcessService::ResolveViewExposureSettings(
  const CompositionView::ViewStateHandle handle,
  const scene::ExposureSettings& requested,
  const std::optional<float> camera_ev) -> const ExposureSettingsState&
{
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
  if (owner && owner->handle == handle && owner->source_loss)
    PreserveRemovedExposureSource(owner->source_loss, handle);
  renderer_.PrepareExposureTransition(handle,
    settings.resolved.authored.enabled
        && settings.resolved.authored.mode == engine::ExposureMode::kAuto
      ? ExposureTransitionPolicy::kRemeter
      : ExposureTransitionPolicy::kPreserve,
    current_sequence_, suppress_transitions || (owner && owner->diagnostic));
  static_cast<void>(
    renderer_.CaptureExposureTransition(handle, current_sequence_));
  return captured_exposure_settings_
    .emplace(key, CapturedExposureSettings { handle, std::move(settings) })
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
            ->TryGetSystem<scene::environment::PostProcessVolume>())
        requested = post->GetExposureSettings();
    }
    static_cast<void>(CaptureViewExposureSettings(source_view_id, source_handle,
      requested, intent->camera_ev, intent->diagnostic, ctx.GetScene()));
  }
  const auto& captured = captured_exposure_settings_.at(key);
  CHECK_F(captured.handle == source_handle);
  auto config = PostProcessConfig {};
  ApplyExposureRevision(config, captured.settings);
  return captured_exposure_sources_
    .emplace(source_view_id,
      postprocess::ExposurePass::Source { .handle = source_handle,
        .config = std::move(config),
        .transition
        = renderer_.CaptureExposureTransition(source_handle, current_sequence_),
        .rejection
        = renderer_.CapturedExposureRejection(source_handle, current_sequence_),
        .lifetime = captured.settings.lifetime })
    .first->second;
}

auto PostProcessService::CaptureRegisteredExposureControls(
  const RenderContext& ctx) -> void
{
  if (captured_control_frame_ == current_sequence_)
    return;
  captured_control_frame_ = current_sequence_;
  auto inherited = scene::ExposureSettings {};
  if (const auto scene = ctx.GetScene(); scene && scene->GetEnvironment()) {
    if (const auto post = scene->GetEnvironment()
          ->TryGetSystem<scene::environment::PostProcessVolume>())
      inherited = post->GetExposureSettings();
  }
  for (const auto& intent : renderer_.GetRegisteredExposureIntents()) {
    if (intent.source_loss)
      PreserveRemovedExposureSource(intent.source_loss, intent.handle);
    // Registered intents already resolve each view's overrides against the
    // renderer defaults. The current scope may belong to a different view.
    const bool suppressed = intent.diagnostic;
    const auto& captured = CaptureViewExposureSettings(intent.view_id,
      intent.handle, intent.settings.value_or(inherited), intent.camera_ev,
      suppressed, ctx.GetScene());
    const auto token
      = renderer_.CaptureExposureTransition(intent.handle, current_sequence_);
    if (!token || suppressed
      || renderer_.CapturedExposureRejection(intent.handle, current_sequence_))
      continue;
    std::optional<ExposureTransitionError> error;
    if (intent.owner != intent.handle) {
      error = ExposureTransitionError::kSharedConsumer;
    } else if (token->policy != ExposureTransitionPolicy::kPreserve
      && (!captured.resolved.authored.enabled
        || captured.resolved.authored.mode != engine::ExposureMode::kAuto)) {
      error = ExposureTransitionError::kNotAuto;
    } else if (token->policy == ExposureTransitionPolicy::kSeedFromEv100
      && !scene::ResolveExposureSeedLogGain(
        captured.resolved, *token->seed_ev)) {
      error = ExposureTransitionError::kUnsupportedSeed;
    }
    if (error)
      renderer_.RejectUnsubmittedExposureTransition(*token, *error);
  }
}

auto PostProcessService::BuildBindings(const Inputs& inputs) const
  -> PostProcessFrameBindings
{
  return BuildBindings(inputs, config_);
}

auto PostProcessService::BuildBindings(const Inputs& inputs,
  const PostProcessConfig& config) const -> PostProcessFrameBindings
{
  return {
    .resolved_scene_color_srv = inputs.scene_signal_srv,
    .scene_depth_srv = inputs.scene_depth_srv,
    .scene_velocity_srv = inputs.scene_velocity_srv,
    .bloom_texture_srv = inputs.bloom_texture_srv,
    .eye_adaptation_srv = inputs.eye_adaptation_srv,
    .eye_adaptation_uav = inputs.eye_adaptation_uav,
    .post_history_srv = inputs.post_history_srv,
    .tone_mapper = config.tone_mapper,
    .metering_mode = config.metering_mode,
    .enable_bloom = config.enable_bloom ? 1U : 0U,
    .enable_auto_exposure = config.enable_auto_exposure ? 1U : 0U,
    .fixed_exposure = config.fixed_exposure,
    .gamma = config.gamma,
    .bloom_intensity = config.bloom_intensity,
    .bloom_threshold = config.bloom_threshold,
    .auto_exposure_speed_up = config.auto_exposure_speed_up,
    .auto_exposure_speed_down = config.auto_exposure_speed_down,
    .auto_exposure_low_percentile = config.auto_exposure_low_percentile,
    .auto_exposure_high_percentile = config.auto_exposure_high_percentile,
    .auto_exposure_min_ev = config.auto_exposure_min_ev,
    .auto_exposure_max_ev = config.auto_exposure_max_ev,
    .auto_exposure_min_log_luminance = config.auto_exposure_min_log_luminance,
    .auto_exposure_log_luminance_range
    = config.auto_exposure_log_luminance_range,
    .auto_exposure_target_luminance = config.auto_exposure_target_luminance,
    .auto_exposure_spot_meter_radius = config.auto_exposure_spot_meter_radius,
    .scene_fallback_srv = inputs.scene_fallback_srv,
    .conversion_report_srv = inputs.checked_resolution
      ? inputs.checked_resolution->suitability_srv
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
  const auto key
    = ExposureCaptureKey { view_id, ctx.current_view.view_state_handle };
  auto captured = captured_exposure_settings_.find(key);
  if (captured == captured_exposure_settings_.end()) {
    CHECK_F(config_.resolved_exposure.has_value());
    auto settings = ExposureSettingsState {
      .resolved = *config_.resolved_exposure,
      .revision = config_.exposure_settings_revision,
      .lifetime
      = renderer_.EnsureExposureLifetime(ctx.current_view.view_state_handle),
    };
    const auto accepted
      = exposure_settings_.find(ctx.current_view.view_state_handle);
    const auto* source = accepted != exposure_settings_.end()
      ? &accepted->second
      : ctx.current_view.view_state_handle
        == CompositionView::kInvalidViewStateHandle
      ? &transient_exposure_settings_
      : nullptr;
    if (source && source->lifetime == settings.lifetime
      && source->revision == settings.revision
      && source->resolved.authored == settings.resolved.authored
      && source->resolved.fixed_scale == settings.resolved.fixed_scale)
      settings = *source;
    renderer_.ObserveExposureWorld(
      ctx.current_view.view_state_handle, ctx.GetScene().get());
    renderer_.PrepareExposureTransition(ctx.current_view.view_state_handle,
      settings.resolved.authored.enabled
          && settings.resolved.authored.mode == engine::ExposureMode::kAuto
        ? ExposureTransitionPolicy::kRemeter
        : ExposureTransitionPolicy::kPreserve,
      ctx.frame_sequence,
      config_.temporary_unit_exposure
        || ctx.shader_debug_mode != ShaderDebugMode::kDisabled
        || ctx.render_mode == RenderMode::kWireframe);
    static_cast<void>(renderer_.CaptureExposureTransition(
      ctx.current_view.view_state_handle, ctx.frame_sequence));
    captured = captured_exposure_settings_
                 .emplace(key,
                   CapturedExposureSettings {
                     ctx.current_view.view_state_handle, std::move(settings) })
                 .first;
  }
  CHECK_F(captured->second.handle == ctx.current_view.view_state_handle,
    "A captured view cannot change its exposure lifetime within a frame");
  return captured->second.settings;
}

auto PostProcessService::PrepareFrameExposure(RenderContext& ctx,
  const bool use_fp32,
  postprocess::ExposurePass::StateLease qualified_candidate)
  -> postprocess::ExposurePass::FrameLease
{
  const auto& settings
    = CaptureConfiguredExposure(ctx.current_view.view_id, ctx);
  auto config = config_;
  ApplyExposureRevision(config, settings);
  config.temporary_unit_exposure = config.temporary_unit_exposure
    || ctx.shader_debug_mode != ShaderDebugMode::kDisabled
    || ctx.render_mode == RenderMode::kWireframe;
  const auto source_handle = ctx.current_view.exposure_view_state_handle;
  const auto* source = !config.temporary_unit_exposure
      && source_handle != CompositionView::kInvalidViewStateHandle
      && source_handle != ctx.current_view.view_state_handle
    ? &CaptureSharedExposureSource(
        ctx, ctx.current_view.exposure_view_id, source_handle)
    : nullptr;
  return exposure_pass_->ResolveFrame(ctx, config,
    { .use_fp32 = use_fp32,
      .qualified_candidate = std::move(qualified_candidate),
      .source = source,
      .transition = renderer_.CaptureExposureTransition(
        ctx.current_view.view_state_handle, ctx.frame_sequence),
      .rejection = renderer_.CapturedExposureRejection(
        ctx.current_view.view_state_handle, ctx.frame_sequence),
      .lifetime = settings.lifetime });
}

auto PostProcessService::PrepareSceneExposure(const ViewId view_id,
  RenderContext& ctx, const Inputs& inputs) -> std::optional<PreparedExposure>
{
  const auto* settings = &CaptureConfiguredExposure(view_id, ctx);
  auto effective_config = config_;
  ApplyExposureRevision(effective_config, *settings);
  auto mask = settings ? settings->mask : nullptr;
  const bool initial_mask_unavailable = settings && settings->revision == 0U
    && (settings->mask_status == ExposureMaskStatus::kPending
      || settings->mask_status == ExposureMaskStatus::kFailed);
  const bool requested_mask_missing = effective_config.resolved_exposure
    && effective_config.resolved_exposure->authored.metering_mask.get() != 0U
    && !mask;
  if (mask) {
    CHECK_LT_F(current_slot_.get(), frame_masks_.size());
    frame_masks_[current_slot_.get()].push_back(mask);
  }
  effective_config.temporary_unit_exposure = config_.temporary_unit_exposure
    || ctx.shader_debug_mode != ShaderDebugMode::kDisabled
    || ctx.render_mode == RenderMode::kWireframe;
  CaptureRegisteredExposureControls(ctx);
  const auto transition = renderer_.CaptureExposureTransition(
    ctx.current_view.view_state_handle, ctx.frame_sequence);
  const auto source_handle = ctx.current_view.exposure_view_state_handle;
  const auto* source = !effective_config.temporary_unit_exposure
      && source_handle != CompositionView::kInvalidViewStateHandle
      && source_handle != ctx.current_view.view_state_handle
    ? &CaptureSharedExposureSource(
        ctx, ctx.current_view.exposure_view_id, source_handle)
    : nullptr;
  const auto exposure = exposure_pass_->Execute(ctx, effective_config,
    postprocess::ExposurePass::Inputs {
      .scene_signal = inputs.scene_signal,
      .scene_signal_srv = inputs.scene_signal_srv,
      .metering_mask = mask ? mask->texture.get() : nullptr,
      .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex,
      .metering_available
      = !initial_mask_unavailable && !requested_mask_missing,
      .transition
      = !effective_config.temporary_unit_exposure ? transition : std::nullopt,
      .source = source,
      .rejection = !effective_config.temporary_unit_exposure
        ? renderer_.CapturedExposureRejection(
            ctx.current_view.view_state_handle, ctx.frame_sequence)
        : std::nullopt,
      .lifetime = settings->lifetime,
    });
  if (exposure.frame && !exposure.state) {
    last_execution_state_ = { .tonemap_requested = true, .view_id = view_id };
    return std::nullopt;
  }
  if (transition && exposure.executed && exposure.state
    && !effective_config.temporary_unit_exposure) {
    EnqueueExposureStatus(*transition, exposure.state, ctx, settings->revision);
  }
  return PreparedExposure { this, exposure, std::move(effective_config),
    view_id, ctx.current_view.view_state_handle, settings->lifetime,
    ctx.frame_sequence };
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
  const PreparedExposure& prepared, const Inputs& inputs,
  graphics::Texture& destination, const ShaderVisibleIndex destination_uav)
  -> bool
{
  ValidatePreparedExposure(ctx.current_view.view_id, ctx, prepared);
  CHECK_NOTNULL_F(prepared.exposure.frame.get());
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
  if (initial_mask_unavailable || (needs_mask && !mask))
    return false;
  return exposure_pass_->ConvertCheckedSceneColor(ctx, prepared.exposure.frame,
    prepared.config,
    { .scene_signal = inputs.scene_signal,
      .scene_signal_srv = inputs.scene_signal_srv,
      .metering_mask = mask ? mask->texture.get() : nullptr,
      .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex },
    destination, destination_uav);
}

auto PostProcessService::Execute(const ViewId view_id, RenderContext& ctx,
  const SceneTextures& scene_textures, const Inputs& inputs,
  const PreparedExposure* prepared_exposure) -> void
{
  auto local_exposure = std::optional<PreparedExposure> {};
  if (!prepared_exposure) {
    local_exposure = PrepareSceneExposure(view_id, ctx, inputs);
    if (!local_exposure) {
      last_execution_state_ = { .tonemap_requested = true, .view_id = view_id };
      return;
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
  if (effective_config.temporary_unit_exposure) {
    bindings.enable_auto_exposure = 0U;
    bindings.fixed_exposure = 1.0F;
  }
  bindings.eye_adaptation_srv = exposure.exposure_buffer_srv;
  bindings.eye_adaptation_uav = exposure.exposure_buffer_uav;
  const auto slot = PublishBindings(view_id, bindings);
  const auto bloom = bloom_pass_->Execute(effective_config, bindings);
  const auto tonemap = tonemap_pass_->Record(ctx, scene_textures,
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
      .tone_mapper = effective_config.tone_mapper,
      .exposure_value = exposure.exposure_value,
      .gamma = effective_config.gamma,
      .bloom_intensity = effective_config.bloom_intensity,
      .background_color = environment::ResolveSceneBackground(ctx),
      .scene_fallback = inputs.scene_fallback,
      .scene_fallback_srv = inputs.scene_fallback_srv,
      .conversion_report = inputs.checked_resolution
        ? inputs.checked_resolution->suitability_buffer.get()
        : nullptr,
      .conversion_report_srv = inputs.checked_resolution
        ? inputs.checked_resolution->suitability_srv
        : kInvalidShaderVisibleIndex,
    });

  last_execution_state_ = {
    .published_bindings = slot != kInvalidShaderVisibleIndex,
    .tonemap_requested = tonemap.requested,
    .tonemap_executed = tonemap.executed,
    .wrote_visible_output = tonemap.wrote_visible_output,
    .bloom_requested = bloom.requested,
    .bloom_executed = bloom.executed,
    .auto_exposure_requested = effective_config.enable_auto_exposure
      && !effective_config.temporary_unit_exposure
      && !exposure.borrowed_exposure && exposure.requested,
    .auto_exposure_executed = effective_config.enable_auto_exposure
      && !effective_config.temporary_unit_exposure
      && !exposure.borrowed_exposure && exposure.executed,
    .used_fixed_exposure = exposure.used_fixed_exposure,
    .view_id = view_id,
    .post_process_frame_slot = slot,
    .exposure_value = exposure.exposure_value,
  };
}

auto PostProcessService::PreserveRemovedExposureSource(
  std::shared_ptr<const ExposureSourceLoss> loss,
  CompositionView::ViewStateHandle only_consumer) -> void
{
  CHECK_NOTNULL_F(loss.get());
  auto source
    = postprocess::ExposurePass::Source { .handle = loss->source_handle,
        .transition = loss->transition,
        .rejection = loss->rejection,
        .lifetime = loss->source_lifetime };
  if (const auto captured
    = captured_exposure_sources_.find(loss->source_view_id);
    captured != captured_exposure_sources_.end()
    && captured->second.lifetime == loss->source_lifetime) {
    source = captured->second;
  } else if (const auto accepted = exposure_settings_.find(loss->source_handle);
    accepted != exposure_settings_.end()
    && accepted->second.lifetime == loss->source_lifetime) {
    ApplyExposureRevision(source.config, accepted->second);
  } else {
    auto requested = loss->settings;
    // No accepted mask revision exists for a never-prepared source.
    if (requested.enabled && requested.mode == engine::ExposureMode::kAuto
      && requested.min_ev != requested.max_ev
      && requested.metering_mask.get() != 0U)
      requested = scene::ExposureSettings {};
    auto resolved = scene::ResolveExposureSettings(requested, loss->camera_ev);
    if (!resolved)
      resolved = scene::ResolveExposureSettings(scene::ExposureSettings {});
    ApplyExposureRevision(source.config,
      ExposureSettingsState {
        .resolved = *resolved, .lifetime = loss->source_lifetime });
  }
  exposure_pass_->PreserveRemovedSource(std::move(loss), source, only_consumer);
}

auto PostProcessService::RemoveViewState(const ViewId view_id,
  const CompositionView::ViewStateHandle view_state_handle) -> void
{
  published_views_.erase(view_id);
  captured_exposure_settings_.erase({ view_id, view_state_handle });
  exposure_settings_.erase(view_state_handle);
  pending_exposure_status_.erase(view_state_handle);
  deferred_exposure_status_.erase(view_state_handle);
  renderer_.RetireExposureTransitions(view_state_handle);
  exposure_pass_->RemoveViewState(view_state_handle);
}

auto PostProcessService::EnqueueExposureStatus(
  const ExposureTransitionToken& token,
  postprocess::ExposurePass::StateLease state, const RenderContext& ctx,
  const std::uint64_t settings_revision) -> void
{
  renderer_.MarkExposureTransitionSubmitted(token);
  auto job = PendingExposureStatus { std::move(state), {}, token,
    ctx.frame_sequence.get(), settings_revision };
  if (!IsExposureStatusNeeded(job))
    return;
  if (!TryEnqueueExposureStatus(job)) {
    DeferExposureStatus(std::move(job));
  } else if (const auto older = deferred_exposure_status_.find(token.target);
    older != deferred_exposure_status_.end()
    && older->second.frame_sequence <= job.frame_sequence) {
    deferred_exposure_status_.erase(older);
  }
}

auto PostProcessService::IsExposureStatusNeeded(
  const PendingExposureStatus& job) const -> bool
{
  return renderer_.NeedsExposureAcknowledgement(job.token);
}

auto PostProcessService::DeferExposureStatus(PendingExposureStatus job) -> void
{
  if (!IsExposureStatusNeeded(job))
    return;
  job.readback.reset();
  const auto found = deferred_exposure_status_.find(job.token.target);
  if (found == deferred_exposure_status_.end()
    || found->second.token.lifetime != job.token.lifetime
    || found->second.frame_sequence <= job.frame_sequence)
    deferred_exposure_status_.insert_or_assign(
      job.token.target, std::move(job));
}

auto PostProcessService::TryEnqueueExposureStatus(PendingExposureStatus job)
  -> bool
{
  auto& pending = pending_exposure_status_[job.token.target];
  if (pending.size() >= frame::kFramesInFlight.get())
    return false;
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return false;
  auto manager = gfx->GetReadbackManager();
  if (!manager)
    return false;
  auto readback = manager->CreateBufferReadback("Exposure completed status");
  if (!readback)
    return false;
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Exposure status readback");
  if (!recorder)
    return false;
  const auto recording = recorder->GetCommandListForInspection();
  if (!recorder->AdoptKnownResourceState(*job.state->status_buffer)) {
    recorder->BeginTrackingResourceState(
      *job.state->status_buffer, graphics::ResourceStates::kCopySource, false);
  }
  const auto ticket = readback->EnqueueCopy(*recorder,
    *job.state->status_buffer, { 0U, sizeof(ExposureCompletedStatus) });
  recorder.reset();
  if (!ticket || !recording || !recording->IsSubmitted()) {
    static_cast<void>(readback->Cancel());
    return false;
  }
  job.readback = std::move(readback);
  pending.push_back(std::move(job));
  return true;
}

auto PostProcessService::PollExposureStatus() -> void
{
  const auto integer = [](const std::array<std::uint32_t, 2>& words) {
    return std::uint64_t { words[0] } | (std::uint64_t { words[1] } << 32U);
  };
  for (auto& [handle, pending] : pending_exposure_status_) {
    while (!pending.empty()) {
      auto& job = pending.front();
      const auto ready = job.readback->IsReady();
      if (ready && !*ready)
        break;
      const auto complete = [&]() -> bool {
        if (!ready)
          return false;
        const auto mapped = job.readback->TryMap();
        if (!mapped || mapped->Bytes().size() < sizeof(ExposureCompletedStatus))
          return false;
        ExposureCompletedStatus status {};
        std::memcpy(&status, mapped->Bytes().data(), sizeof(status));
        if (integer(status.view_state_identity) != job.token.lifetime
          || integer(status.frame_sequence) != job.frame_sequence
          || integer(status.settings_revision) != job.settings_revision
          || integer(status.requested_generation) != job.token.generation)
          return false;
        std::optional<ExposureTransitionError> rejection;
        if ((status.flags & 8U) != 0U) {
          rejection = status.transition_rejection_reason == 1U
            ? ExposureTransitionError::kNotAuto
            : status.transition_rejection_reason == 3U
            ? ExposureTransitionError::kSharedConsumer
            : ExposureTransitionError::kUnsupportedSeed;
        }
        renderer_.CompleteExposureTransition(
          job.token, integer(status.applied_generation), rejection);
        return true;
      };
      if (!complete())
        DeferExposureStatus(std::move(job));
      pending.pop_front();
    }
  }
  std::erase_if(pending_exposure_status_,
    [](const auto& entry) { return entry.second.empty(); });
  for (auto it = deferred_exposure_status_.begin();
    it != deferred_exposure_status_.end();) {
    if (!IsExposureStatusNeeded(it->second)
      || TryEnqueueExposureStatus(it->second))
      it = deferred_exposure_status_.erase(it);
    else
      ++it;
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
