//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples::ui {

namespace {
  constexpr float kDefaultMinLogLuminance = -12.0F;
  constexpr float kDefaultLogLuminanceRange = 25.0F;
  constexpr float kDefaultLowPercentile = 0.1F;
  constexpr float kDefaultHighPercentile = 0.9F;
  constexpr float kDefaultAdaptationSpeedUp = 3.0F;
  constexpr float kDefaultAdaptationSpeedDown = 3.0F;
  constexpr float kDefaultTargetLuminance = 0.18F;
  constexpr float kDefaultSpotMeterRadius = 0.2F;
  constexpr engine::MeteringMode kDefaultMeteringMode
    = engine::MeteringMode::kAverage;

  auto ToSceneExposureMode(const engine::ExposureMode mode)
    -> engine::ExposureMode
  {
    switch (mode) {
    case engine::ExposureMode::kManual:
      return engine::ExposureMode::kManual;
    case engine::ExposureMode::kAuto:
      return engine::ExposureMode::kAuto;
    case engine::ExposureMode::kManualCamera:
      return engine::ExposureMode::kManualCamera;
    }
    return engine::ExposureMode::kManual;
  }

  auto EnsurePostProcessVolume(observer_ptr<scene::Scene> scene)
    -> observer_ptr<scene::environment::PostProcessVolume>
  {
    if (!scene) {
      return nullptr;
    }

    auto env = scene->GetEnvironment();
    if (!env) {
      scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
      env = scene->GetEnvironment();
    }

    if (!env) {
      return nullptr;
    }

    if (auto pp = env->TryGetSystem<scene::environment::PostProcessVolume>()) {
      return pp;
    }

    auto& pp = env->AddSystem<scene::environment::PostProcessVolume>();
    return observer_ptr { &pp };
  }

  struct PersistedPostProcessState {
    engine::ExposureMode exposure_mode { engine::ExposureMode::kManual };
    float manual_exposure_ev { 9.7F };
    float exposure_compensation { 0.0F };
    float exposure_key { 12.5F };
    bool exposure_enabled { true };
    float auto_exposure_speed_up { kDefaultAdaptationSpeedUp };
    float auto_exposure_speed_down { kDefaultAdaptationSpeedDown };
    float auto_exposure_low_percentile { kDefaultLowPercentile };
    float auto_exposure_high_percentile { kDefaultHighPercentile };
    float auto_exposure_min_ev { -6.0F };
    float auto_exposure_max_ev { 16.0F };
    float auto_exposure_min_log_luminance { kDefaultMinLogLuminance };
    float auto_exposure_log_luminance_range { kDefaultLogLuminanceRange };
    float auto_exposure_target_luminance { kDefaultTargetLuminance };
    float auto_exposure_spot_meter_radius { kDefaultSpotMeterRadius };
    engine::MeteringMode auto_exposure_metering_mode { kDefaultMeteringMode };
    bool tonemapping_enabled { true };
    engine::ToneMapper tone_mapper { engine::ToneMapper::kAcesFitted };
    float gamma { 2.2F };
  };

  auto ApplyExposureToPipeline(
    observer_ptr<renderer::RenderingPipeline> /*pipeline*/,
    const engine::ExposureMode /*mode*/, const float /*manual_ev*/,
    const float /*manual_camera_ev*/, const float /*compensation_ev*/,
    const float /*exposure_key*/, const bool /*enabled*/) -> void
  {
  }

  auto ResolveSceneToneMapper(const PersistedPostProcessState& state)
    -> engine::ToneMapper
  {
    return state.tonemapping_enabled ? state.tone_mapper
                                     : engine::ToneMapper::kNone;
  }

  auto ApplyPersistedPostProcessToScene(observer_ptr<scene::Scene> scene,
    const PersistedPostProcessState& state) -> void
  {
    const auto pp = EnsurePostProcessVolume(scene);
    if (!pp) {
      return;
    }

    pp->SetExposureEnabled(state.exposure_enabled);
    pp->SetExposureMode(ToSceneExposureMode(state.exposure_mode));
    pp->SetManualExposureEv(state.manual_exposure_ev);
    pp->SetExposureCompensationEv(state.exposure_compensation);
    pp->SetExposureKey(state.exposure_key);
    pp->SetAutoExposureAdaptationSpeeds(
      state.auto_exposure_speed_up, state.auto_exposure_speed_down);
    pp->SetAutoExposureHistogramPercentiles(
      state.auto_exposure_low_percentile, state.auto_exposure_high_percentile);
    pp->SetAutoExposureRangeEv(
      state.auto_exposure_min_ev, state.auto_exposure_max_ev);
    pp->SetAutoExposureHistogramWindow(state.auto_exposure_min_log_luminance,
      state.auto_exposure_log_luminance_range);
    pp->SetAutoExposureTargetLuminance(state.auto_exposure_target_luminance);
    pp->SetAutoExposureSpotMeterRadius(state.auto_exposure_spot_meter_radius);
    pp->SetAutoExposureMeteringMode(state.auto_exposure_metering_mode);
    pp->SetToneMapper(ResolveSceneToneMapper(state));
    pp->SetDisplayGamma(state.gamma);
  }

  auto CapturePersistedPostProcessState(
    const PostProcessSettingsService& service) -> PersistedPostProcessState
  {
    return {
      .exposure_mode = service.GetExposureMode(),
      .manual_exposure_ev = service.GetManualExposureEv(),
      .exposure_compensation = service.GetExposureCompensation(),
      .exposure_key = service.GetExposureKey(),
      .exposure_enabled = service.GetExposureEnabled(),
      .auto_exposure_speed_up = service.GetAutoExposureAdaptationSpeedUp(),
      .auto_exposure_speed_down = service.GetAutoExposureAdaptationSpeedDown(),
      .auto_exposure_low_percentile = service.GetAutoExposureLowPercentile(),
      .auto_exposure_high_percentile = service.GetAutoExposureHighPercentile(),
      .auto_exposure_min_ev = service.GetAutoExposureMinEv(),
      .auto_exposure_max_ev = service.GetAutoExposureMaxEv(),
      .auto_exposure_min_log_luminance
      = service.GetAutoExposureMinLogLuminance(),
      .auto_exposure_log_luminance_range
      = service.GetAutoExposureLogLuminanceRange(),
      .auto_exposure_target_luminance
      = service.GetAutoExposureTargetLuminance(),
      .auto_exposure_spot_meter_radius
      = service.GetAutoExposureSpotMeterRadius(),
      .auto_exposure_metering_mode = service.GetAutoExposureMeteringMode(),
      .tonemapping_enabled = service.GetTonemappingEnabled(),
      .tone_mapper = service.GetToneMapper(),
      .gamma = service.GetGamma(),
    };
  }

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

  auto ResolveManualCameraEv(
    observer_ptr<oxygen::examples::CameraSettingsService> camera_settings)
    -> float
  {
    if (auto* exposure = ResolveActiveCameraExposure(camera_settings)) {
      return exposure->GetEv();
    }

    const oxygen::scene::CameraExposure fallback {};
    return fallback.GetEv();
  }
} // namespace

auto PostProcessSettingsService::Initialize(
  observer_ptr<renderer::RenderingPipeline> pipeline) -> void
{
  pipeline_ = pipeline;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::BindCameraSettings(
  observer_ptr<CameraSettingsService> camera_settings) -> void
{
  camera_settings_ = camera_settings;
  epoch_++;

  if (pipeline_) {
    ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
      ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
      GetExposureKey(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::BindScene(observer_ptr<scene::Scene> scene)
  -> void
{
  scene_ = scene;
  epoch_++;

  if (pipeline_) {
    ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
      ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
      GetExposureKey(), GetExposureEnabled());
  }
  SyncScenePostProcessState();
}

// Exposure

auto PostProcessSettingsService::GetExposureMode() const -> engine::ExposureMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const auto raw = settings->GetFloat(kExposureModeKey).value_or(0.0F);
  switch (static_cast<int>(std::round(raw))) {
  case 2:
    return engine::ExposureMode::kAuto;
  case 1:
    return engine::ExposureMode::kManualCamera;
  case 0:
  default:
    return engine::ExposureMode::kManual;
  }
}

auto PostProcessSettingsService::GetExposureEnabled() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kExposureEnabledKey).value_or(true);
}

auto PostProcessSettingsService::SetExposureEnabled(bool enabled) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kExposureEnabledKey, enabled);
  epoch_++;

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
    ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
    GetExposureKey(), enabled);
  SyncScenePostProcessState();

  if (enabled) {
    UpdateAutoExposureTarget();
  }
}

auto PostProcessSettingsService::SetExposureMode(engine::ExposureMode mode)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kExposureModeKey, static_cast<float>(mode));
  epoch_++;

  // Ensure target is updated when switching modes (e.g. into Auto)
  UpdateAutoExposureTarget();

  ApplyExposureToPipeline(pipeline_, mode, GetManualExposureEv(),
    ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
    GetExposureKey(), GetExposureEnabled());
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetManualExposureEv() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kExposureManualEVKey).value_or(9.7F);
}

auto PostProcessSettingsService::SetManualExposureEv(float ev) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  ev = std::clamp(ev, -16.0F, 24.0F);
  settings->SetFloat(kExposureManualEVKey, ev);
  epoch_++;

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), ev,
    ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
    GetExposureKey(), GetExposureEnabled());
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetManualCameraAperture() const -> float
{
  if (const auto* exposure = ResolveActiveCameraExposure(camera_settings_)) {
    return exposure->aperture_f;
  }
  return 11.0F;
}

auto PostProcessSettingsService::SetManualCameraAperture(float aperture) -> void
{
  if (auto* exposure = ResolveActiveCameraExposure(camera_settings_)) {
    exposure->aperture_f = aperture;
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
      ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
      GetExposureKey(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualCameraShutterRate() const -> float
{
  if (const auto* exposure = ResolveActiveCameraExposure(camera_settings_)) {
    return exposure->shutter_rate;
  }
  return 125.0F;
}

auto PostProcessSettingsService::SetManualCameraShutterRate(float shutter_rate)
  -> void
{
  if (auto* exposure = ResolveActiveCameraExposure(camera_settings_)) {
    exposure->shutter_rate = shutter_rate;
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
      ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
      GetExposureKey(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualCameraIso() const -> float
{
  if (const auto* exposure = ResolveActiveCameraExposure(camera_settings_)) {
    return exposure->iso;
  }
  return 100.0F;
}

auto PostProcessSettingsService::SetManualCameraIso(float iso) -> void
{
  if (auto* exposure = ResolveActiveCameraExposure(camera_settings_)) {
    exposure->iso = iso;
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
      ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
      GetExposureKey(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualCameraEv() const -> float
{
  return ResolveManualCameraEv(camera_settings_);
}

auto PostProcessSettingsService::GetExposureCompensation() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kExposureCompensationKey).value_or(0.0F);
}

auto PostProcessSettingsService::SetExposureCompensation(float stops) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  settings->SetFloat(kExposureCompensationKey, stops);
  epoch_++;

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
    ResolveManualCameraEv(camera_settings_), stops, GetExposureKey(),
    GetExposureEnabled());
  SyncScenePostProcessState();

  UpdateAutoExposureTarget();
}

auto PostProcessSettingsService::GetExposureKey() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kExposureKeyKey).value_or(12.5F);
}

auto PostProcessSettingsService::SetExposureKey(float exposure_key) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  settings->SetFloat(kExposureKeyKey, exposure_key);
  epoch_++;

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
    ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
    exposure_key, GetExposureEnabled());
  SyncScenePostProcessState();

  // Also update auto-exposure target if in use (as Key affects Target scalar)
  UpdateAutoExposureTarget();
}

// Auto Exposure

auto PostProcessSettingsService::GetAutoExposureAdaptationSpeedUp() const
  -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureSpeedUpKey)
    .value_or(kDefaultAdaptationSpeedUp);
}

auto PostProcessSettingsService::SetAutoExposureAdaptationSpeedUp(float speed)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureSpeedUpKey, speed);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureAdaptationSpeedDown() const
  -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureSpeedDownKey)
    .value_or(kDefaultAdaptationSpeedDown);
}

auto PostProcessSettingsService::SetAutoExposureAdaptationSpeedDown(float speed)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureSpeedDownKey, speed);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureLowPercentile() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureLowPercentileKey)
    .value_or(kDefaultLowPercentile);
}

auto PostProcessSettingsService::SetAutoExposureLowPercentile(float percentile)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureLowPercentileKey, percentile);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureHighPercentile() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureHighPercentileKey)
    .value_or(kDefaultHighPercentile);
}

auto PostProcessSettingsService::SetAutoExposureHighPercentile(float percentile)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureHighPercentileKey, percentile);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureMinEv() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureMinEvKey).value_or(-6.0F);
}

auto PostProcessSettingsService::SetAutoExposureMinEv(float min_ev) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const auto clamped_min_ev = std::clamp(min_ev, -16.0F, 24.0F);
  const auto max_ev = std::max(GetAutoExposureMaxEv(), clamped_min_ev);
  settings->SetFloat(kAutoExposureMinEvKey, clamped_min_ev);
  settings->SetFloat(kAutoExposureMaxEvKey, max_ev);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureMaxEv() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureMaxEvKey).value_or(16.0F);
}

auto PostProcessSettingsService::SetAutoExposureMaxEv(float max_ev) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const auto clamped_max_ev = std::clamp(max_ev, -16.0F, 24.0F);
  const auto min_ev = std::min(GetAutoExposureMinEv(), clamped_max_ev);
  settings->SetFloat(kAutoExposureMinEvKey, min_ev);
  settings->SetFloat(kAutoExposureMaxEvKey, clamped_max_ev);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureMinLogLuminance() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureMinLogLumKey)
    .value_or(kDefaultMinLogLuminance);
}

auto PostProcessSettingsService::SetAutoExposureMinLogLuminance(float luminance)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureMinLogLumKey, luminance);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureLogLuminanceRange() const
  -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureLogLumRangeKey)
    .value_or(kDefaultLogLuminanceRange);
}

auto PostProcessSettingsService::SetAutoExposureLogLuminanceRange(float range)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureLogLumRangeKey, range);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureTargetLuminance() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureTargetLumKey)
    .value_or(kDefaultTargetLuminance);
}

auto PostProcessSettingsService::SetAutoExposureTargetLuminance(float luminance)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureTargetLumKey, luminance);
  epoch_++;
  UpdateAutoExposureTarget();
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureSpotMeterRadius() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureSpotRadiusKey)
    .value_or(kDefaultSpotMeterRadius);
}

auto PostProcessSettingsService::SetAutoExposureSpotMeterRadius(float radius)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureSpotRadiusKey, radius);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetAutoExposureMeteringMode() const
  -> engine::MeteringMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const auto raw = settings->GetFloat(kAutoExposureMeteringKey).value_or(0.0F);
  switch (static_cast<int>(std::round(raw))) {
  case 2:
    return engine::MeteringMode::kSpot;
  case 1:
    return engine::MeteringMode::kCenterWeighted;
  case 0:
  default:
    return engine::MeteringMode::kAverage;
  }
}

auto PostProcessSettingsService::SetAutoExposureMeteringMode(
  engine::MeteringMode mode) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureMeteringKey, static_cast<float>(mode));
  epoch_++;

  SyncScenePostProcessState();
}

// Tonemapping

auto PostProcessSettingsService::GetTonemappingEnabled() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kTonemappingEnabledKey).value_or(true);
}

auto PostProcessSettingsService::SetTonemappingEnabled(bool enabled) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kTonemappingEnabledKey, enabled);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetToneMapper() const -> engine::ToneMapper
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const auto raw
    = settings->GetFloat(kToneMapperKey)
        .value_or(static_cast<float>(engine::ToneMapper::kAcesFitted));
  switch (static_cast<int>(std::round(raw))) {
  case 0:
    return engine::ToneMapper::kNone;
  case 2:
    return engine::ToneMapper::kFilmic;
  case 3:
    return engine::ToneMapper::kReinhard;
  case 1:
  default:
    return engine::ToneMapper::kAcesFitted;
  }
}

auto PostProcessSettingsService::SetToneMapper(engine::ToneMapper mode) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kToneMapperKey, static_cast<float>(mode));
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::GetGamma() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kGammaKey).value_or(2.2F);
}

auto PostProcessSettingsService::SetGamma(float gamma) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kGammaKey, gamma);
  epoch_++;
  SyncScenePostProcessState();
}

auto PostProcessSettingsService::ResetToDefaults() -> void
{
  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return;
  }

  // Reset to defaults by explicitly setting values
  settings->SetFloat(kExposureModeKey, 0.0F);
  settings->SetBool(kExposureEnabledKey, true);
  settings->SetFloat(kExposureManualEVKey, 9.7F);
  settings->SetFloat(kExposureCompensationKey, 0.0F);
  settings->SetFloat(kExposureKeyKey, 12.5F);

  settings->SetBool(kTonemappingEnabledKey, true);
  settings->SetFloat(
    kToneMapperKey, static_cast<float>(engine::ToneMapper::kAcesFitted));
  settings->SetFloat(kGammaKey, 2.2F);

  settings->SetFloat(kAutoExposureSpeedUpKey, kDefaultAdaptationSpeedUp);
  settings->SetFloat(kAutoExposureSpeedDownKey, kDefaultAdaptationSpeedDown);
  settings->SetFloat(kAutoExposureLowPercentileKey, kDefaultLowPercentile);
  settings->SetFloat(kAutoExposureHighPercentileKey, kDefaultHighPercentile);
  settings->SetFloat(kAutoExposureMinEvKey, -6.0F);
  settings->SetFloat(kAutoExposureMaxEvKey, 16.0F);
  settings->SetFloat(kAutoExposureMinLogLumKey, kDefaultMinLogLuminance);
  settings->SetFloat(kAutoExposureLogLumRangeKey, kDefaultLogLuminanceRange);
  settings->SetFloat(kAutoExposureTargetLumKey, kDefaultTargetLuminance);
  settings->SetFloat(kAutoExposureSpotRadiusKey, kDefaultSpotMeterRadius);
  settings->SetFloat(
    kAutoExposureMeteringKey, static_cast<float>(kDefaultMeteringMode));

  epoch_++;

  // Re-apply everything
  Initialize(pipeline_);
}

auto PostProcessSettingsService::ResetAutoExposureDefaults() -> void
{
  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return;
  }

  // Reset only Auto Exposure settings
  settings->SetFloat(kAutoExposureSpeedUpKey, kDefaultAdaptationSpeedUp);
  settings->SetFloat(kAutoExposureSpeedDownKey, kDefaultAdaptationSpeedDown);
  settings->SetFloat(kAutoExposureLowPercentileKey, kDefaultLowPercentile);
  settings->SetFloat(kAutoExposureHighPercentileKey, kDefaultHighPercentile);
  settings->SetFloat(kAutoExposureMinEvKey, -6.0F);
  settings->SetFloat(kAutoExposureMaxEvKey, 16.0F);
  settings->SetFloat(kAutoExposureMinLogLumKey, kDefaultMinLogLuminance);
  settings->SetFloat(kAutoExposureLogLumRangeKey, kDefaultLogLuminanceRange);
  settings->SetFloat(kAutoExposureTargetLumKey, kDefaultTargetLuminance);
  settings->SetFloat(kAutoExposureSpotRadiusKey, kDefaultSpotMeterRadius);
  settings->SetFloat(
    kAutoExposureMeteringKey, static_cast<float>(kDefaultMeteringMode));

  epoch_++;
  Initialize(pipeline_);
}

auto PostProcessSettingsService::UpdateAutoExposureTarget() -> void { }

auto PostProcessSettingsService::ResetAutoExposure(float /*initial_ev*/) -> void
{
}

auto PostProcessSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  if (camera_settings_) {
    const auto& camera = camera_settings_->GetActiveCamera();
    const std::string camera_id
      = camera.IsAlive() ? camera.GetName() : std::string {};
    if (camera_id != last_camera_id_) {
      last_camera_id_ = camera_id;
      epoch_.fetch_add(1, std::memory_order_acq_rel);
    }
  }
  return epoch_.load(std::memory_order_acquire);
}

auto PostProcessSettingsService::SyncScenePostProcessState() -> void
{
  ApplyPersistedPostProcessToScene(
    scene_, CapturePersistedPostProcessState(*this));
}

} // namespace oxygen::examples::ui
