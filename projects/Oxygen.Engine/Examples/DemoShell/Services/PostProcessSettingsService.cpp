//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include "DemoShell/Runtime/SceneActivationPolicy.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Scene/Camera/CameraExposure.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/ExposureSettingsStatus.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::examples::ui {
namespace {
  constexpr float kDefaultManualEv = 9.7F;
  constexpr float kDefaultDarkAdaptationRate = 3.0F;

  auto ResolveActiveCameraExposure(
    observer_ptr<oxygen::examples::CameraSettingsService> camera_settings)
    -> oxygen::scene::CameraExposure*
  {
    if (!camera_settings) {
      return nullptr;
    }

    auto& active_camera = camera_settings->GetActiveCamera();
    if (!active_camera.IsAlive()) {
      return nullptr;
    }

    if (auto cam_ref
      = active_camera.GetCameraAs<oxygen::scene::PerspectiveCamera>();
      cam_ref) {
      return &cam_ref->get().Exposure();
    }

    if (auto cam_ref
      = active_camera.GetCameraAs<oxygen::scene::OrthographicCamera>();
      cam_ref) {
      return &cam_ref->get().Exposure();
    }

    return nullptr;
  }

} // namespace

auto PostProcessSettingsService::Defaults() -> State
{
  auto state = State {};
  state.exposure.mode = engine::ExposureMode::kManual;
  state.exposure.manual_ev = kDefaultManualEv;
  state.exposure.key = engine::kExposureCalibrationKey;
  state.exposure.speed_down = kDefaultDarkAdaptationRate;
  return state;
}

auto PostProcessSettingsService::FloatBindings()
  -> std::span<const ExposureFloatBinding>
{
  static constexpr auto kFields = std::array {
    ExposureFloatBinding {
      .key = kExposureManualEVKey,
      .member = &scene::ExposureSettings::manual_ev,
      .automatic = false,
    },
    ExposureFloatBinding {
      .key = kExposureCompensationKey,
      .member = &scene::ExposureSettings::compensation_ev,
      .automatic = false,
    },
    ExposureFloatBinding {
      .key = kExposureKeyKey,
      .member = &scene::ExposureSettings::key,
      .automatic = false,
    },
    ExposureFloatBinding {
      .key = kAutoExposureSpeedUpKey,
      .member = &scene::ExposureSettings::speed_up,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureSpeedDownKey,
      .member = &scene::ExposureSettings::speed_down,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureLowPercentileKey,
      .member = &scene::ExposureSettings::low_percentile,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureHighPercentileKey,
      .member = &scene::ExposureSettings::high_percentile,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureMinEvKey,
      .member = &scene::ExposureSettings::min_ev,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureMaxEvKey,
      .member = &scene::ExposureSettings::max_ev,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureMinLogLumKey,
      .member = &scene::ExposureSettings::min_log_luminance,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureLogLumRangeKey,
      .member = &scene::ExposureSettings::log_luminance_range,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureTargetLumKey,
      .member = &scene::ExposureSettings::target_luminance,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kAutoExposureSpotRadiusKey,
      .member = &scene::ExposureSettings::spot_meter_radius,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kBlackInfluenceKey,
      .member = &scene::ExposureSettings::black_influence,
      .automatic = true,
    },
    ExposureFloatBinding {
      .key = kTransitionDistanceKey,
      .member = &scene::ExposureSettings::transition_distance,
      .automatic = true,
    },
  };
  return kFields;
}

auto PostProcessSettingsService::CaptureSceneDefaults() const -> State
{
  auto result = activation_policy_ == SceneActivationPolicy::kExperimentOwned
    ? State {}
    : Defaults();
  if (scene_ && scene_->GetEnvironment()) {
    if (const auto post = scene_->GetEnvironment()
          ->TryGetSystem<scene::environment::PostProcessVolume>()) {
      result.exposure = post->GetExposureSettings();
      result.tonemapping_enabled
        = post->GetToneMapper() != engine::ToneMapper::kNone;
      result.tone_mapper = result.tonemapping_enabled
        ? post->GetToneMapper()
        : engine::ToneMapper::kAcesFitted;
      result.gamma = post->GetDisplayGamma();
    }
  }
  return result;
}

auto PostProcessSettingsService::EnsureStateLoaded() const -> void
{
  if (state_initialized_) {
    return;
  }
  scene_defaults_ = CaptureSceneDefaults();
  state_ = scene_defaults_;
  state_initialized_ = true;
  use_scene_mask_ = true;
  if (activation_policy_ == SceneActivationPolicy::kExperimentOwned) {
    return;
  }
  const auto saved = SettingsService::ForDemoApp();
  if (!saved) {
    return;
  }
  auto requested = state_.exposure;
  bool valid_encoding = true;
  for (const auto& binding : FloatBindings()) {
    if (const auto value = saved->GetFloat(binding.key)) {
      requested.*binding.member = *value;
    }
  }
  if (const auto mode = saved->GetFloat(kExposureModeKey)) {
    valid_encoding = *mode == 0.0F || *mode == 1.0F || *mode == 2.0F;
    if (valid_encoding) {
      requested.mode
        = static_cast<engine::ExposureMode>(static_cast<uint32_t>(*mode));
    }
  }
  if (const auto mode = saved->GetFloat(kAutoExposureMeteringKey)) {
    const bool valid = *mode == 0.0F || *mode == 1.0F || *mode == 2.0F;
    valid_encoding = valid_encoding && valid;
    if (valid) {
      requested.metering_mode
        = static_cast<engine::MeteringMode>(static_cast<uint32_t>(*mode));
    }
  }
  requested.enabled
    = saved->GetBool(kExposureEnabledKey).value_or(requested.enabled);
  use_scene_mask_ = saved->GetBool(kUseSceneMaskKey).value_or(true);
  requested.metering_mask = use_scene_mask_
    ? scene_defaults_.exposure.metering_mask
    : content::ResourceKey {};
  if (const auto encoded = saved->GetString(kCurveKey)) {
    const auto curve = nlohmann::json::parse(*encoded, nullptr, false);
    if (!curve.is_array()
      || curve.size() > engine::kMaxExposureCompensationCurveKeys) {
      valid_encoding = false;
    } else {
      requested.compensation_curve.clear();
      for (const auto& key : curve) {
        if (!key.is_object() || key.size() != 2U || !key.contains("metered_ev")
          || !key.contains("compensation_ev")
          || !key.at("metered_ev").is_number()
          || !key.at("compensation_ev").is_number()) {
          valid_encoding = false;
          break;
        }
        requested.compensation_curve.push_back({
          .metered_ev = key.at("metered_ev").get<float>(),
          .compensation_ev = key.at("compensation_ev").get<float>(),
        });
      }
    }
  }
  if (valid_encoding && ValidateExposure(requested)) {
    state_.exposure = std::move(requested);
  } else {
    if (!valid_encoding) {
      validation_error_
        = "Saved exposure mode, metering mode or curve is invalid.";
    }
    LOG_F(WARNING, "Saved exposure preferences ignored: {}", validation_error_);
    use_scene_mask_ = true;
  }
  state_.tonemapping_enabled = saved->GetBool(kTonemappingEnabledKey)
                                 .value_or(state_.tonemapping_enabled);
  if (const auto mode = saved->GetFloat(kToneMapperKey)) {
    if (std::isfinite(*mode) && *mode >= 0.0F
      && *mode <= static_cast<float>(engine::ToneMapper::kReinhard)
      && std::trunc(*mode) == *mode) {
      state_.tone_mapper
        = static_cast<engine::ToneMapper>(static_cast<uint32_t>(*mode));
    } else {
      LOG_F(WARNING, "Saved tone mapper ignored: invalid value {}", *mode);
    }
  }
  if (const auto gamma = saved->GetFloat(kGammaKey)) {
    if (std::isfinite(*gamma) && *gamma >= engine::kMinDisplayGamma) {
      state_.gamma = *gamma;
    } else {
      LOG_F(WARNING, "Saved display gamma ignored: invalid value {}", *gamma);
    }
  }
  if (transient_exposure_mode_ || transient_manual_exposure_ev_
    || transient_exposure_enabled_) {
    auto transient = state_.exposure;
    transient.mode = transient_exposure_mode_.value_or(transient.mode);
    transient.manual_ev
      = transient_manual_exposure_ev_.value_or(transient.manual_ev);
    transient.enabled = transient_exposure_enabled_.value_or(transient.enabled);
    if (ValidateExposure(transient)) {
      state_.exposure = std::move(transient);
    }
  }
}

auto PostProcessSettingsService::ValidateExposure(
  const scene::ExposureSettings& requested) const -> bool
{
  const auto* camera = ResolveActiveCameraExposure(camera_settings_);
  const auto resolved = scene::ResolveExposureSettings(requested,
    camera != nullptr ? std::optional { camera->GetEv() } : std::nullopt);
  if (!resolved
    && resolved.error() != scene::ExposureSettingsError::kMissingCameraEv) {
    validation_error_ = std::string(scene::to_string(resolved.error()));
    return false;
  }
  validation_error_.clear();
  return true;
}

auto PostProcessSettingsService::CommitExposure(
  const scene::ExposureSettings& requested) -> bool
{
  EnsureStateLoaded();
  if (!ValidateExposure(requested)) {
    LOG_F(WARNING, "Exposure edit rejected: {}", validation_error_);
    return false;
  }
  state_.exposure = requested;
  ++epoch_;
  SyncScenePostProcessState();
  return true;
}

auto PostProcessSettingsService::PersistExposure(
  const std::string_view key) const -> void
{
  if (activation_policy_ == SceneActivationPolicy::kExperimentOwned) {
    return;
  }
  const auto saved = SettingsService::ForDemoApp();
  if (!saved || !state_initialized_) {
    return;
  }
  for (const auto& binding : FloatBindings()) {
    if (key == binding.key) {
      saved->SetFloat(key, state_.exposure.*binding.member);
      return;
    }
  }
  if (key == kExposureModeKey) {
    saved->SetFloat(key, static_cast<float>(state_.exposure.mode));
  } else if (key == kAutoExposureMeteringKey) {
    saved->SetFloat(key, static_cast<float>(state_.exposure.metering_mode));
  } else if (key == kExposureEnabledKey) {
    saved->SetBool(key, state_.exposure.enabled);
  } else if (key == kUseSceneMaskKey) {
    saved->SetBool(key, use_scene_mask_);
  } else if (key == kCurveKey) {
    auto curve = nlohmann::json::array();
    for (const auto& point : state_.exposure.compensation_curve) {
      curve.push_back({
        { "metered_ev", point.metered_ev },
        { "compensation_ev", point.compensation_ev },
      });
    }
    saved->SetString(key, curve.dump());
  }
}

auto PostProcessSettingsService::PersistAllExposure() const -> void
{
  for (const auto& field : FloatBindings()) {
    PersistExposure(field.key);
  }
  for (const auto* const key : {
         kExposureModeKey,
         kExposureEnabledKey,
         kAutoExposureMeteringKey,
         kCurveKey,
         kUseSceneMaskKey,
       }) {
    PersistExposure(key);
  }
}

auto PostProcessSettingsService::SetSceneActivationPolicy(
  const SceneActivationPolicy policy) -> void
{
  if (policy != SceneActivationPolicy::kRestorePreferences
    && policy != SceneActivationPolicy::kExperimentOwned) {
    throw std::invalid_argument("Unknown scene activation policy");
  }
  if (activation_policy_ != policy) {
    activation_policy_ = policy;
    state_initialized_ = false;
    transient_exposure_mode_.reset();
    transient_manual_exposure_ev_.reset();
    transient_exposure_enabled_.reset();
    ++epoch_;
  }
}

auto PostProcessSettingsService::GetSceneActivationPolicy() const noexcept
  -> SceneActivationPolicy
{
  return activation_policy_;
}

auto PostProcessSettingsService::BindCameraSettings(
  observer_ptr<CameraSettingsService> camera_settings) -> void
{
  camera_settings_ = camera_settings;
  ++epoch_;
}

auto PostProcessSettingsService::BindScene(observer_ptr<scene::Scene> scene)
  -> void
{
  if (scene_ != scene
    || activation_policy_ == SceneActivationPolicy::kExperimentOwned) {
    scene_ = scene;
    ++scene_revision_;
    state_initialized_ = false;
    main_view_id_.reset();
  }
  EnsureStateLoaded();
  ++epoch_;
  if (activation_policy_ == SceneActivationPolicy::kRestorePreferences) {
    SyncScenePostProcessState();
  }
}

auto PostProcessSettingsService::OnFrameStart() -> void
{
  if (const auto* camera = ResolveActiveCameraExposure(camera_settings_)) {
    if (!observed_camera_exposure_
      || observed_camera_exposure_->aperture_f != camera->aperture_f
      || observed_camera_exposure_->shutter_rate != camera->shutter_rate
      || observed_camera_exposure_->iso != camera->iso) {
      observed_camera_exposure_ = *camera;
      ++epoch_;
    }
  } else if (observed_camera_exposure_) {
    observed_camera_exposure_.reset();
    ++epoch_;
  }
  if (activation_policy_ != SceneActivationPolicy::kExperimentOwned
    || !state_initialized_ || !scene_) {
    return;
  }
  const auto environment = scene_->GetEnvironment();
  const auto post = environment
    ? environment->TryGetSystem<scene::environment::PostProcessVolume>()
    : nullptr;
  const auto effective_tone = state_.tonemapping_enabled
    ? state_.tone_mapper
    : engine::ToneMapper::kNone;
  const bool changed = post ? post->GetExposureSettings() != state_.exposure
      || post->GetToneMapper() != effective_tone
      || post->GetDisplayGamma() != state_.gamma
                            : state_.exposure != State {}.exposure
      || effective_tone != engine::ToneMapper::kAcesFitted
      || state_.gamma != State {}.gamma;
  if (changed) {
    state_ = CaptureSceneDefaults();
    ++epoch_;
  }
}

auto PostProcessSettingsService::GetSceneRevision() const noexcept
  -> std::uint64_t
{
  return scene_revision_;
}

auto PostProcessSettingsService::BindMainView(const ViewId view_id) -> void
{
  main_view_id_ = view_id;
}
auto PostProcessSettingsService::BindVortexRenderer(
  observer_ptr<vortex::Renderer> renderer) -> void
{
  vortex_renderer_ = renderer;
}
auto PostProcessSettingsService::GetExposureStatus() const
  -> std::optional<vortex::ExposureSettingsStatus>
{
  if (!vortex_renderer_ || !main_view_id_) {
    return std::nullopt;
  }
  const auto published_view
    = vortex_renderer_->ResolvePublishedRuntimeViewId(*main_view_id_);
  return vortex_renderer_->InspectExposureSettings(published_view);
}
auto PostProcessSettingsService::GetExposureSettings() const
  -> scene::ExposureSettings
{
  EnsureStateLoaded();
  return state_.exposure;
}
auto PostProcessSettingsService::GetValidationError() const noexcept
  -> std::string_view
{
  return validation_error_;
}
auto PostProcessSettingsService::HasActiveCamera() const -> bool
{
  return ResolveActiveCameraExposure(camera_settings_) != nullptr;
}
auto PostProcessSettingsService::HasSceneMeteringMask() const -> bool
{
  EnsureStateLoaded();
  return scene_defaults_.exposure.metering_mask.get() != 0U;
}
auto PostProcessSettingsService::GetUseSceneMeteringMask() const -> bool
{
  EnsureStateLoaded();
  return use_scene_mask_;
}

auto PostProcessSettingsService::SetUseSceneMeteringMask(const bool enabled)
  -> void
{
  auto next = GetExposureSettings();
  next.metering_mask = enabled ? scene_defaults_.exposure.metering_mask
                               : content::ResourceKey {};
  if (CommitExposure(next)) {
    use_scene_mask_ = enabled;
    PersistExposure(kUseSceneMaskKey);
  }
}

auto PostProcessSettingsService::TrySetExposureSettings(
  const scene::ExposureSettings& requested) -> bool
{
  EnsureStateLoaded();
  if (requested.metering_mask.get() != 0U
    && requested.metering_mask != scene_defaults_.exposure.metering_mask) {
    validation_error_
      = "Choose the scene's authored metering mask or disable it.";
    LOG_F(WARNING, "Exposure edit rejected: {}", validation_error_);
    return false;
  }
  if (!CommitExposure(requested)) {
    return false;
  }
  transient_exposure_mode_.reset();
  transient_manual_exposure_ev_.reset();
  transient_exposure_enabled_.reset();
  use_scene_mask_
    = requested.metering_mask == scene_defaults_.exposure.metering_mask;
  PersistAllExposure();
  return true;
}

auto PostProcessSettingsService::SetExposureCompensationCurve(
  const std::span<const scene::ExposureCompensationKey> keys) -> bool
{
  if (keys.size() > engine::kMaxExposureCompensationCurveKeys) {
    validation_error_
      = "An exposure compensation curve supports at most 64 keys.";
    LOG_F(WARNING, "Exposure curve edit rejected: {} keys", keys.size());
    return false;
  }
  auto next = GetExposureSettings();
  next.compensation_curve.assign(keys.begin(), keys.end());
  if (!CommitExposure(next)) {
    return false;
  }
  PersistExposure(kCurveKey);
  return true;
}

auto PostProcessSettingsService::ApplyExposurePreset(
  const engine::ExposureMode mode, const float manual_ev, const bool enabled,
  const bool persist) -> void
{
  auto next = GetExposureSettings();
  next.mode = mode;
  next.manual_ev = manual_ev;
  next.enabled = enabled;
  if (!CommitExposure(next)) {
    return;
  }
  if (persist) {
    transient_exposure_mode_.reset();
    transient_manual_exposure_ev_.reset();
    transient_exposure_enabled_.reset();
    PersistExposure(kExposureModeKey);
    PersistExposure(kExposureManualEVKey);
    PersistExposure(kExposureEnabledKey);
  } else {
    transient_exposure_mode_ = mode;
    transient_manual_exposure_ev_ = manual_ev;
    transient_exposure_enabled_ = enabled;
  }
  ResetAutoExposure(manual_ev);
}

auto PostProcessSettingsService::GetExposureMode() const -> engine::ExposureMode
{
  return GetExposureSettings().mode;
}
auto PostProcessSettingsService::SetExposureMode(
  const engine::ExposureMode mode) -> void
{
  auto next = GetExposureSettings();
  next.mode = mode;
  if (CommitExposure(next)) {
    transient_exposure_mode_.reset();
    PersistExposure(kExposureModeKey);
  }
}
auto PostProcessSettingsService::GetExposureEnabled() const -> bool
{
  return GetExposureSettings().enabled;
}
auto PostProcessSettingsService::SetExposureEnabled(const bool enabled) -> void
{
  auto next = GetExposureSettings();
  next.enabled = enabled;
  if (CommitExposure(next)) {
    transient_exposure_enabled_.reset();
    PersistExposure(kExposureEnabledKey);
  }
}

auto PostProcessSettingsService::SetExposureFloat(
  float scene::ExposureSettings::* member, const float value,
  const std::string_view key) -> void
{
  auto next = GetExposureSettings();
  next.*member = value;
  if (CommitExposure(next)) {
    if (member == &scene::ExposureSettings::manual_ev) {
      transient_manual_exposure_ev_.reset();
    }
    PersistExposure(key);
  }
}

auto PostProcessSettingsService::GetManualExposureEv() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.manual_ev;
}
auto PostProcessSettingsService::SetManualExposureEv(const float value) -> void
{
  SetExposureFloat(
    &scene::ExposureSettings::manual_ev, value, kExposureManualEVKey);
}

auto PostProcessSettingsService::GetExposureCompensation() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.compensation_ev;
}
auto PostProcessSettingsService::SetExposureCompensation(const float value)
  -> void
{
  SetExposureFloat(
    &scene::ExposureSettings::compensation_ev, value, kExposureCompensationKey);
}

auto PostProcessSettingsService::GetExposureKey() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.key;
}
auto PostProcessSettingsService::SetExposureKey(const float value) -> void
{
  SetExposureFloat(&scene::ExposureSettings::key, value, kExposureKeyKey);
}

auto PostProcessSettingsService::GetAutoExposureAdaptationSpeedUp() const
  -> float
{
  EnsureStateLoaded();
  return state_.exposure.speed_up;
}
auto PostProcessSettingsService::SetAutoExposureAdaptationSpeedUp(
  const float value) -> void
{
  SetExposureFloat(
    &scene::ExposureSettings::speed_up, value, kAutoExposureSpeedUpKey);
}

auto PostProcessSettingsService::GetAutoExposureAdaptationSpeedDown() const
  -> float
{
  EnsureStateLoaded();
  return state_.exposure.speed_down;
}
auto PostProcessSettingsService::SetAutoExposureAdaptationSpeedDown(
  const float value) -> void
{
  SetExposureFloat(
    &scene::ExposureSettings::speed_down, value, kAutoExposureSpeedDownKey);
}

auto PostProcessSettingsService::GetAutoExposureLowPercentile() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.low_percentile;
}
auto PostProcessSettingsService::SetAutoExposureLowPercentile(const float value)
  -> void
{
  SetExposureFloat(&scene::ExposureSettings::low_percentile, value,
    kAutoExposureLowPercentileKey);
}

auto PostProcessSettingsService::GetAutoExposureHighPercentile() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.high_percentile;
}
auto PostProcessSettingsService::SetAutoExposureHighPercentile(
  const float value) -> void
{
  SetExposureFloat(&scene::ExposureSettings::high_percentile, value,
    kAutoExposureHighPercentileKey);
}

auto PostProcessSettingsService::GetAutoExposureMinEv() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.min_ev;
}
auto PostProcessSettingsService::SetAutoExposureMinEv(const float value) -> void
{
  SetExposureFloat(
    &scene::ExposureSettings::min_ev, value, kAutoExposureMinEvKey);
}

auto PostProcessSettingsService::GetAutoExposureMaxEv() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.max_ev;
}
auto PostProcessSettingsService::SetAutoExposureMaxEv(const float value) -> void
{
  SetExposureFloat(
    &scene::ExposureSettings::max_ev, value, kAutoExposureMaxEvKey);
}

auto PostProcessSettingsService::GetAutoExposureMinLogLuminance() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.min_log_luminance;
}
auto PostProcessSettingsService::SetAutoExposureMinLogLuminance(
  const float value) -> void
{
  SetExposureFloat(&scene::ExposureSettings::min_log_luminance, value,
    kAutoExposureMinLogLumKey);
}

auto PostProcessSettingsService::GetAutoExposureLogLuminanceRange() const
  -> float
{
  EnsureStateLoaded();
  return state_.exposure.log_luminance_range;
}
auto PostProcessSettingsService::SetAutoExposureLogLuminanceRange(
  const float value) -> void
{
  SetExposureFloat(&scene::ExposureSettings::log_luminance_range, value,
    kAutoExposureLogLumRangeKey);
}

auto PostProcessSettingsService::GetAutoExposureTargetLuminance() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.target_luminance;
}
auto PostProcessSettingsService::SetAutoExposureTargetLuminance(
  const float value) -> void
{
  SetExposureFloat(&scene::ExposureSettings::target_luminance, value,
    kAutoExposureTargetLumKey);
}

auto PostProcessSettingsService::GetAutoExposureSpotMeterRadius() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.spot_meter_radius;
}
auto PostProcessSettingsService::SetAutoExposureSpotMeterRadius(
  const float value) -> void
{
  SetExposureFloat(&scene::ExposureSettings::spot_meter_radius, value,
    kAutoExposureSpotRadiusKey);
}

auto PostProcessSettingsService::GetAutoExposureBlackInfluence() const -> float
{
  EnsureStateLoaded();
  return state_.exposure.black_influence;
}
auto PostProcessSettingsService::SetAutoExposureBlackInfluence(
  const float value) -> void
{
  SetExposureFloat(
    &scene::ExposureSettings::black_influence, value, kBlackInfluenceKey);
}

auto PostProcessSettingsService::GetAutoExposureTransitionDistance() const
  -> float
{
  EnsureStateLoaded();
  return state_.exposure.transition_distance;
}
auto PostProcessSettingsService::SetAutoExposureTransitionDistance(
  const float value) -> void
{
  SetExposureFloat(&scene::ExposureSettings::transition_distance, value,
    kTransitionDistanceKey);
}

auto PostProcessSettingsService::GetManualCameraAperture() const -> float
{
  const auto* camera = ResolveActiveCameraExposure(camera_settings_);
  return camera != nullptr ? camera->aperture_f
                           : engine::kDefaultCameraApertureF;
}
auto PostProcessSettingsService::SetManualCameraAperture(const float value)
  -> void
{
  SetCameraExposureFloat(&scene::CameraExposure::aperture_f, value);
}

auto PostProcessSettingsService::GetManualCameraShutterRate() const -> float
{
  const auto* camera = ResolveActiveCameraExposure(camera_settings_);
  return camera != nullptr ? camera->shutter_rate
                           : engine::kDefaultCameraShutterRate;
}
auto PostProcessSettingsService::SetManualCameraShutterRate(const float value)
  -> void
{
  SetCameraExposureFloat(&scene::CameraExposure::shutter_rate, value);
}

auto PostProcessSettingsService::GetManualCameraIso() const -> float
{
  const auto* camera = ResolveActiveCameraExposure(camera_settings_);
  return camera != nullptr ? camera->iso : engine::kDefaultCameraIso;
}
auto PostProcessSettingsService::SetManualCameraIso(const float value) -> void
{
  SetCameraExposureFloat(&scene::CameraExposure::iso, value);
}

auto PostProcessSettingsService::GetManualCameraEv() const -> float
{
  const auto* camera = ResolveActiveCameraExposure(camera_settings_);
  return camera != nullptr ? camera->GetEv()
                           : std::numeric_limits<float>::quiet_NaN();
}

auto PostProcessSettingsService::SetCameraExposureFloat(
  float scene::CameraExposure::* member, const float value) -> void
{
  auto* camera = ResolveActiveCameraExposure(camera_settings_);
  if (camera == nullptr || !std::isfinite(value) || value <= 0.0F) {
    validation_error_ = camera == nullptr
      ? "Select an active camera."
      : "Camera exposure values must be finite and positive.";
    LOG_F(WARNING, "Camera exposure edit rejected: {}", validation_error_);
    return;
  }
  EnsureStateLoaded();
  auto next = *camera;
  next.*member = value;
  const auto resolved
    = scene::ResolveExposureSettings(state_.exposure, next.GetEv());
  if (!resolved) {
    validation_error_ = std::string(scene::to_string(resolved.error()));
    LOG_F(WARNING, "Camera exposure edit rejected: {}", validation_error_);
    return;
  }
  *camera = next;
  validation_error_.clear();
  ++epoch_;
}

auto PostProcessSettingsService::SetAutoExposureRange(
  const ExposureRange bounds) -> void
{
  auto next = GetExposureSettings();
  next.min_ev = bounds.minimum;
  next.max_ev = bounds.maximum;
  if (CommitExposure(next)) {
    PersistExposure(kAutoExposureMinEvKey);
    PersistExposure(kAutoExposureMaxEvKey);
  }
}
auto PostProcessSettingsService::SetAutoExposurePercentiles(
  const ExposureRange bounds) -> void
{
  auto next = GetExposureSettings();
  next.low_percentile = bounds.minimum;
  next.high_percentile = bounds.maximum;
  if (CommitExposure(next)) {
    PersistExposure(kAutoExposureLowPercentileKey);
    PersistExposure(kAutoExposureHighPercentileKey);
  }
}
auto PostProcessSettingsService::SetAutoExposureHistogramWindow(
  const ExposureRange bounds) -> void
{
  auto next = GetExposureSettings();
  next.min_log_luminance = bounds.minimum;
  next.log_luminance_range = bounds.maximum - bounds.minimum;
  if (CommitExposure(next)) {
    PersistExposure(kAutoExposureMinLogLumKey);
    PersistExposure(kAutoExposureLogLumRangeKey);
  }
}
auto PostProcessSettingsService::GetAutoExposureMeteringMode() const
  -> engine::MeteringMode
{
  EnsureStateLoaded();
  return state_.exposure.metering_mode;
}
auto PostProcessSettingsService::SetAutoExposureMeteringMode(
  const engine::MeteringMode mode) -> void
{
  auto next = GetExposureSettings();
  next.metering_mode = mode;
  if (CommitExposure(next)) {
    PersistExposure(kAutoExposureMeteringKey);
  }
}

auto PostProcessSettingsService::GetTonemappingEnabled() const -> bool
{
  EnsureStateLoaded();
  return state_.tonemapping_enabled;
}
auto PostProcessSettingsService::SetTonemappingEnabled(const bool enabled)
  -> void
{
  EnsureStateLoaded();
  state_.tonemapping_enabled = enabled;
  if (activation_policy_ == SceneActivationPolicy::kRestorePreferences) {
    SettingsService::ForDemoApp()->SetBool(kTonemappingEnabledKey, enabled);
  }
  ++epoch_;
  SyncScenePostProcessState();
}
auto PostProcessSettingsService::GetToneMapper() const -> engine::ToneMapper
{
  EnsureStateLoaded();
  return state_.tone_mapper;
}
auto PostProcessSettingsService::SetToneMapper(const engine::ToneMapper mode)
  -> void
{
  if (mode > engine::ToneMapper::kReinhard) {
    validation_error_ = "Unknown tone mapper.";
    LOG_F(WARNING, "Tone mapper edit rejected: unknown value");
    return;
  }
  EnsureStateLoaded();
  state_.tone_mapper = mode;
  if (activation_policy_ == SceneActivationPolicy::kRestorePreferences) {
    SettingsService::ForDemoApp()->SetFloat(
      kToneMapperKey, static_cast<float>(mode));
  }
  validation_error_.clear();
  ++epoch_;
  SyncScenePostProcessState();
}
auto PostProcessSettingsService::GetGamma() const -> float
{
  EnsureStateLoaded();
  return state_.gamma;
}
auto PostProcessSettingsService::SetGamma(const float gamma) -> void
{
  if (!std::isfinite(gamma) || gamma < engine::kMinDisplayGamma) {
    validation_error_ = "Display gamma must be finite and at least 0.001.";
    LOG_F(WARNING, "Display gamma edit rejected: {}", gamma);
    return;
  }
  EnsureStateLoaded();
  state_.gamma = gamma;
  if (activation_policy_ == SceneActivationPolicy::kRestorePreferences) {
    SettingsService::ForDemoApp()->SetFloat(kGammaKey, gamma);
  }
  validation_error_.clear();
  ++epoch_;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::ResetToDefaults() -> void
{
  EnsureStateLoaded();
  state_ = activation_policy_ == SceneActivationPolicy::kExperimentOwned
    ? scene_defaults_
    : Defaults();
  state_.exposure.metering_mask = scene_defaults_.exposure.metering_mask;
  use_scene_mask_ = true;
  transient_exposure_mode_.reset();
  transient_manual_exposure_ev_.reset();
  transient_exposure_enabled_.reset();
  validation_error_.clear();
  PersistAllExposure();
  if (activation_policy_ == SceneActivationPolicy::kRestorePreferences) {
    const auto saved = SettingsService::ForDemoApp();
    saved->SetBool(kTonemappingEnabledKey, state_.tonemapping_enabled);
    saved->SetFloat(kToneMapperKey, static_cast<float>(state_.tone_mapper));
    saved->SetFloat(kGammaKey, state_.gamma);
  }
  ++epoch_;
  SyncScenePostProcessState();
  LOG_F(
    INFO, "Post-process controls reset ({})", to_string(activation_policy_));
}
auto PostProcessSettingsService::ResetAutoExposureDefaults() -> void
{
  auto next = GetExposureSettings();
  const auto defaults
    = activation_policy_ == SceneActivationPolicy::kExperimentOwned
    ? scene_defaults_
    : Defaults();
  for (const auto& binding : FloatBindings()) {
    if (binding.automatic) {
      next.*binding.member = defaults.exposure.*binding.member;
    }
  }
  next.metering_mode = defaults.exposure.metering_mode;
  next.compensation_curve = defaults.exposure.compensation_curve;
  next.metering_mask = scene_defaults_.exposure.metering_mask;
  if (CommitExposure(next)) {
    use_scene_mask_ = true;
    for (const auto& binding : FloatBindings()) {
      if (binding.automatic) {
        PersistExposure(binding.key);
      }
    }
    PersistExposure(kAutoExposureMeteringKey);
    PersistExposure(kCurveKey);
    PersistExposure(kUseSceneMaskKey);
  }
}

auto PostProcessSettingsService::ResetAutoExposure(const float initial_ev)
  -> void
{
  if (!vortex_renderer_) {
    return;
  }
  for (const auto owner : vortex_renderer_->GetExposureOwners()) {
    const auto queued = vortex_renderer_->QueueExposureTransition(
      owner, vortex::ExposureTransitionPolicy::kSeedFromEv100, initial_ev);
    if (!queued) {
      LOG_F(WARNING,
        "Exposure seed request rejected for view state {} (error {})",
        owner.get(), static_cast<unsigned>(queued.error()));
    }
  }
}

auto PostProcessSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto PostProcessSettingsService::SyncScenePostProcessState() -> void
{
  if (!scene_ || !state_initialized_) {
    return;
  }
  if (!scene_->GetEnvironment()) {
    scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  }
  auto environment = scene_->GetEnvironment();
  auto post
    = environment->TryGetSystem<scene::environment::PostProcessVolume>();
  if (!post) {
    post = observer_ptr {
      &environment->AddSystem<scene::environment::PostProcessVolume>()
    };
  }
  post->SetExposureSettings(state_.exposure);
  post->SetToneMapper(state_.tonemapping_enabled ? state_.tone_mapper
                                                 : engine::ToneMapper::kNone);
  post->SetDisplayGamma(state_.gamma);
}

} // namespace oxygen::examples::ui
