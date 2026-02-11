//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/AutoExposurePass.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples::ui {

namespace {
  auto ToSceneExposureMode(const engine::ExposureMode mode)
    -> scene::environment::ExposureMode
  {
    switch (mode) {
    case engine::ExposureMode::kManual:
      return scene::environment::ExposureMode::kManual;
    case engine::ExposureMode::kAuto:
      return scene::environment::ExposureMode::kAuto;
    case engine::ExposureMode::kManualCamera:
      return scene::environment::ExposureMode::kManualCamera;
    }
    return scene::environment::ExposureMode::kManual;
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

  auto ApplyExposureToPipeline(observer_ptr<RenderingPipeline> pipeline,
    const engine::ExposureMode mode, const float manual_ev,
    const float manual_camera_ev, const float compensation_ev,
    const float exposure_key, const bool enabled) -> void
  {
    if (!pipeline) {
      return;
    }

    pipeline->SetExposureMode(mode);

    // Always calculate exposure from manual settings as a baseline/initial
    // value for the pipeline. This ensures that when switching to Auto, the
    // pipeline (and auto-exposure history) starts with a reasonable value
    // instead of 1.0 (EV 0) which would cause a massive flash.
    const float ev = (mode == engine::ExposureMode::kManualCamera)
      ? manual_camera_ev
      : manual_ev;

    // Standard photometric exposure formula properties:
    // H = q * L / N^2
    // We use the same formula as Manual mode:
    const float exposure
      = (1.0F / 12.5F) * std::exp2(compensation_ev - ev) * exposure_key;

    if (!enabled) {
      pipeline->SetExposureValue(1.0F);
      return;
    }

    pipeline->SetExposureValue(exposure);

    // Auto exposure has temporal state (history). When EV-related inputs change
    // (or when switching modes), re-seed the history so the new settings take
    // effect immediately rather than being perceived as a compounding drift.
    if (mode == engine::ExposureMode::kAuto) {
      pipeline->ResetAutoExposure(ev);
    }
  }

  auto ApplyExposureToScene(observer_ptr<scene::Scene> scene,
    const engine::ExposureMode mode, const float manual_ev,
    const float compensation_ev, const float exposure_key, const bool enabled,
    const engine::MeteringMode metering_mode) -> void
  {
    const auto pp = EnsurePostProcessVolume(scene);
    if (!pp) {
      return;
    }

    pp->SetExposureEnabled(enabled);
    pp->SetExposureMode(ToSceneExposureMode(mode));
    pp->SetManualExposureEv(manual_ev);
    pp->SetExposureCompensationEv(compensation_ev);
    pp->SetExposureKey(exposure_key);
    pp->SetAutoExposureMeteringMode(metering_mode);
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
  observer_ptr<RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;

  // Push initial state
  if (pipeline_) {
    ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
      ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
      GetExposureKey(), GetExposureEnabled());
    pipeline_->SetToneMapper(
      GetTonemappingEnabled() ? GetToneMapper() : engine::ToneMapper::kNone);

    pipeline_->SetAutoExposureAdaptationSpeedUp(
      GetAutoExposureAdaptationSpeedUp());
    pipeline_->SetAutoExposureAdaptationSpeedDown(
      GetAutoExposureAdaptationSpeedDown());
    pipeline_->SetAutoExposureLowPercentile(GetAutoExposureLowPercentile());
    pipeline_->SetAutoExposureHighPercentile(GetAutoExposureHighPercentile());
    pipeline_->SetAutoExposureMinLogLuminance(GetAutoExposureMinLogLuminance());
    pipeline_->SetAutoExposureLogLuminanceRange(
      GetAutoExposureLogLuminanceRange());
    // Target luminance is set via helper to account for compensation
    UpdateAutoExposureTarget();
    pipeline_->SetAutoExposureMeteringMode(GetAutoExposureMeteringMode());
  }

  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEv(),
    GetExposureCompensation(), GetExposureKey(), GetExposureEnabled(),
    GetAutoExposureMeteringMode());
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
  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEv(),
    GetExposureCompensation(), GetExposureKey(), GetExposureEnabled(),
    GetAutoExposureMeteringMode());
}

// Exposure

auto PostProcessSettingsService::GetExposureMode() const -> engine::ExposureMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  auto val = settings->GetString(kExposureModeKey).value_or("manual");
  if (val == "auto") {
    return engine::ExposureMode::kAuto;
  }
  if (val == "manual_camera") {
    return engine::ExposureMode::kManualCamera;
  }
  return engine::ExposureMode::kManual;
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

  {
    const auto mode = GetExposureMode();
    const float manual_ev = GetManualExposureEv();
    const float camera_ev = ResolveManualCameraEv(camera_settings_);
    const float comp_ev = GetExposureCompensation();
    const float key = GetExposureKey();
    const float used_ev
      = (mode == engine::ExposureMode::kManualCamera) ? camera_ev : manual_ev;
    const float baseline_exposure
      = (1.0F / 12.5F) * std::exp2(comp_ev - used_ev) * key;
    LOG_F(INFO,
      "PostProcessSettings: exposure enabled={} (mode={}, manual_ev={:.3f}, cam_ev={:.3f}, used_ev={:.3f}, comp_ev={:.3f}, key={:.3f}, baseline={:.6f})",
      enabled, engine::to_string(mode), manual_ev, camera_ev, used_ev, comp_ev,
      key, baseline_exposure);
  }

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
    ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
    GetExposureKey(), enabled);
  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEv(),
    GetExposureCompensation(), GetExposureKey(), enabled,
    GetAutoExposureMeteringMode());

  if (enabled) {
    UpdateAutoExposureTarget();
  }
}

auto PostProcessSettingsService::SetExposureMode(engine::ExposureMode mode)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetString(kExposureModeKey, engine::to_string(mode));
  epoch_++;

  {
    const float manual_ev = GetManualExposureEv();
    const float camera_ev = ResolveManualCameraEv(camera_settings_);
    const float comp_ev = GetExposureCompensation();
    const float key = GetExposureKey();
    const bool enabled = GetExposureEnabled();
    const float used_ev
      = (mode == engine::ExposureMode::kManualCamera) ? camera_ev : manual_ev;
    const float baseline_exposure
      = (1.0F / 12.5F) * std::exp2(comp_ev - used_ev) * key;
    LOG_F(INFO,
      "PostProcessSettings: exposure mode={} (enabled={}, manual_ev={:.3f}, cam_ev={:.3f}, used_ev={:.3f}, comp_ev={:.3f}, key={:.3f}, baseline={:.6f})",
      engine::to_string(mode), enabled, manual_ev, camera_ev, used_ev, comp_ev,
      key, baseline_exposure);
  }

  // Ensure target is updated when switching modes (e.g. into Auto)
  UpdateAutoExposureTarget();

  ApplyExposureToPipeline(pipeline_, mode, GetManualExposureEv(),
    ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
    GetExposureKey(), GetExposureEnabled());
  ApplyExposureToScene(scene_, mode, GetManualExposureEv(),
    GetExposureCompensation(), GetExposureKey(), GetExposureEnabled(),
    GetAutoExposureMeteringMode());
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
  ev = std::max(ev, 0.0F);
  settings->SetFloat(kExposureManualEVKey, ev);
  epoch_++;

  {
    const auto mode = GetExposureMode();
    const float camera_ev = ResolveManualCameraEv(camera_settings_);
    const float comp_ev = GetExposureCompensation();
    const float key = GetExposureKey();
    const bool enabled = GetExposureEnabled();
    const float used_ev
      = (mode == engine::ExposureMode::kManualCamera) ? camera_ev : ev;
    const float baseline_exposure
      = (1.0F / 12.5F) * std::exp2(comp_ev - used_ev) * key;
    LOG_F(INFO,
      "PostProcessSettings: manual EV set {:.3f} (enabled={}, mode={}, cam_ev={:.3f}, used_ev={:.3f}, comp_ev={:.3f}, key={:.3f}, baseline={:.6f})",
      ev, enabled, engine::to_string(mode), camera_ev, used_ev, comp_ev, key,
      baseline_exposure);
  }

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), ev,
    ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
    GetExposureKey(), GetExposureEnabled());
  ApplyExposureToScene(scene_, GetExposureMode(), ev, GetExposureCompensation(),
    GetExposureKey(), GetExposureEnabled(), GetAutoExposureMeteringMode());
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

  {
    const auto mode = GetExposureMode();
    const float manual_ev = GetManualExposureEv();
    const float camera_ev = ResolveManualCameraEv(camera_settings_);
    const float key = GetExposureKey();
    const bool enabled = GetExposureEnabled();
    const float used_ev = (mode == engine::ExposureMode::kManualCamera)
      ? camera_ev
      : manual_ev;
    const float baseline_exposure
      = (1.0F / 12.5F) * std::exp2(stops - used_ev) * key;
    LOG_F(INFO,
      "PostProcessSettings: exposure comp_ev={:.3f} (enabled={}, mode={}, manual_ev={:.3f}, cam_ev={:.3f}, used_ev={:.3f}, key={:.3f}, baseline={:.6f})",
      stops, enabled, engine::to_string(mode), manual_ev, camera_ev, used_ev,
      key, baseline_exposure);
  }

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
    ResolveManualCameraEv(camera_settings_), stops, GetExposureKey(),
    GetExposureEnabled());
  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEv(), stops,
    GetExposureKey(), GetExposureEnabled(), GetAutoExposureMeteringMode());

  UpdateAutoExposureTarget();
}

auto PostProcessSettingsService::GetExposureKey() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kExposureKeyKey).value_or(10.0F);
}

auto PostProcessSettingsService::SetExposureKey(float exposure_key) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  settings->SetFloat(kExposureKeyKey, exposure_key);
  epoch_++;

  {
    const auto mode = GetExposureMode();
    const float manual_ev = GetManualExposureEv();
    const float camera_ev = ResolveManualCameraEv(camera_settings_);
    const float comp_ev = GetExposureCompensation();
    const bool enabled = GetExposureEnabled();
    const float used_ev = (mode == engine::ExposureMode::kManualCamera)
      ? camera_ev
      : manual_ev;
    const float baseline_exposure
      = (1.0F / 12.5F) * std::exp2(comp_ev - used_ev) * exposure_key;
    LOG_F(INFO,
      "PostProcessSettings: exposure key={:.3f} (enabled={}, mode={}, manual_ev={:.3f}, cam_ev={:.3f}, used_ev={:.3f}, comp_ev={:.3f}, baseline={:.6f})",
      exposure_key, enabled, engine::to_string(mode), manual_ev, camera_ev,
      used_ev, comp_ev, baseline_exposure);
  }

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), GetManualExposureEv(),
    ResolveManualCameraEv(camera_settings_), GetExposureCompensation(),
    exposure_key, GetExposureEnabled());
  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEv(),
    GetExposureCompensation(), exposure_key, GetExposureEnabled(),
    GetAutoExposureMeteringMode());

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
    .value_or(engine::AutoExposurePassConfig::kDefaultAdaptationSpeedUp);
}

auto PostProcessSettingsService::SetAutoExposureAdaptationSpeedUp(float speed)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureSpeedUpKey, speed);
  epoch_++;
  if (pipeline_) {
    pipeline_->SetAutoExposureAdaptationSpeedUp(speed);
  }
}

auto PostProcessSettingsService::GetAutoExposureAdaptationSpeedDown() const
  -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureSpeedDownKey)
    .value_or(engine::AutoExposurePassConfig::kDefaultAdaptationSpeedDown);
}

auto PostProcessSettingsService::SetAutoExposureAdaptationSpeedDown(float speed)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureSpeedDownKey, speed);
  epoch_++;
  if (pipeline_) {
    pipeline_->SetAutoExposureAdaptationSpeedDown(speed);
  }
}

auto PostProcessSettingsService::GetAutoExposureLowPercentile() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureLowPercentileKey)
    .value_or(engine::AutoExposurePassConfig::kDefaultLowPercentile);
}

auto PostProcessSettingsService::SetAutoExposureLowPercentile(float percentile)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureLowPercentileKey, percentile);
  epoch_++;
  if (pipeline_) {
    pipeline_->SetAutoExposureLowPercentile(percentile);
  }
}

auto PostProcessSettingsService::GetAutoExposureHighPercentile() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureHighPercentileKey)
    .value_or(engine::AutoExposurePassConfig::kDefaultHighPercentile);
}

auto PostProcessSettingsService::SetAutoExposureHighPercentile(float percentile)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureHighPercentileKey, percentile);
  epoch_++;
  if (pipeline_) {
    pipeline_->SetAutoExposureHighPercentile(percentile);
  }
}

auto PostProcessSettingsService::GetAutoExposureMinLogLuminance() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureMinLogLumKey)
    .value_or(engine::AutoExposurePassConfig::kDefaultMinLogLuminance);
}

auto PostProcessSettingsService::SetAutoExposureMinLogLuminance(float luminance)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureMinLogLumKey, luminance);
  epoch_++;
  if (pipeline_) {
    pipeline_->SetAutoExposureMinLogLuminance(luminance);
  }
}

auto PostProcessSettingsService::GetAutoExposureLogLuminanceRange() const
  -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureLogLumRangeKey)
    .value_or(engine::AutoExposurePassConfig::kDefaultLogLuminanceRange);
}

auto PostProcessSettingsService::SetAutoExposureLogLuminanceRange(float range)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureLogLumRangeKey, range);
  epoch_++;
  if (pipeline_) {
    pipeline_->SetAutoExposureLogLuminanceRange(range);
  }
}

auto PostProcessSettingsService::GetAutoExposureTargetLuminance() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kAutoExposureTargetLumKey)
    .value_or(engine::AutoExposurePassConfig::kDefaultTargetLuminance);
}

auto PostProcessSettingsService::SetAutoExposureTargetLuminance(float luminance)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAutoExposureTargetLumKey, luminance);
  epoch_++;
  UpdateAutoExposureTarget();
}

auto PostProcessSettingsService::GetAutoExposureMeteringMode() const
  -> engine::MeteringMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  auto val = settings->GetString(kAutoExposureMeteringKey)
               .value_or(std::string(engine::to_string(
                 engine::AutoExposurePassConfig::kDefaultMeteringMode)));
  if (val == "average") {
    return engine::MeteringMode::kAverage;
  }
  if (val == "center_weighted") {
    return engine::MeteringMode::kCenterWeighted;
  }
  if (val == "spot") {
    return engine::MeteringMode::kSpot;
  }
  return engine::MeteringMode::kAverage;
}

auto PostProcessSettingsService::SetAutoExposureMeteringMode(
  engine::MeteringMode mode) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetString(
    kAutoExposureMeteringKey, std::string(engine::to_string(mode)));
  epoch_++;
  if (pipeline_) {
    pipeline_->SetAutoExposureMeteringMode(mode);
  }

  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEv(),
    GetExposureCompensation(), GetExposureKey(), GetExposureEnabled(), mode);
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

  scene::environment::ToneMapper scene_mapper
    = scene::environment::ToneMapper::kAcesFitted;
  bool has_scene_ppv = false;
  if (scene_) {
    if (const auto env = scene_->GetEnvironment()) {
      if (const auto pp
        = env->TryGetSystem<scene::environment::PostProcessVolume>()) {
        has_scene_ppv = true;
        scene_mapper = pp->GetToneMapper();
      }
    }
  }

  LOG_F(INFO,
    "PostProcessSettings: tonemapping enabled set {} (pipeline={}, will_set_mapper={}, stored_mapper={}, scene_ppv={}, scene_mapper={})",
    enabled, pipeline_ != nullptr,
    enabled ? engine::to_string(GetToneMapper()) : engine::to_string(engine::ToneMapper::kNone),
    engine::to_string(GetToneMapper()), has_scene_ppv,
    static_cast<uint32_t>(scene_mapper));

  if (pipeline_) {
    pipeline_->SetToneMapper(
      enabled ? GetToneMapper() : engine::ToneMapper::kNone);
  }
}

auto PostProcessSettingsService::GetToneMapper() const -> engine::ToneMapper
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  auto val = settings->GetString(kToneMapperKey).value_or("aces");
  if (val == "reinhard") {
    return engine::ToneMapper::kReinhard;
  }
  if (val == "aces") {
    return engine::ToneMapper::kAcesFitted;
  }
  if (val == "filmic") {
    return engine::ToneMapper::kFilmic;
  }
  if (val == "none") {
    return engine::ToneMapper::kNone;
  }
  return engine::ToneMapper::kAcesFitted;
}

auto PostProcessSettingsService::SetToneMapper(engine::ToneMapper mode) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetString(kToneMapperKey, engine::to_string(mode));
  epoch_++;

  scene::environment::ToneMapper scene_mapper
    = scene::environment::ToneMapper::kAcesFitted;
  bool has_scene_ppv = false;
  if (scene_) {
    if (const auto env = scene_->GetEnvironment()) {
      if (const auto pp
        = env->TryGetSystem<scene::environment::PostProcessVolume>()) {
        has_scene_ppv = true;
        scene_mapper = pp->GetToneMapper();
      }
    }
  }

  LOG_F(INFO,
    "PostProcessSettings: tone mapper set {} (enabled={}, pipeline={}, scene_ppv={}, scene_mapper={})",
    engine::to_string(mode), GetTonemappingEnabled(), pipeline_ != nullptr,
    has_scene_ppv, static_cast<uint32_t>(scene_mapper));

  if (pipeline_ && GetTonemappingEnabled()) {
    pipeline_->SetToneMapper(mode);
  }
}

auto PostProcessSettingsService::ResetToDefaults() -> void
{
  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return;
  }

  // Reset to defaults by explicitly setting values
  settings->SetString(kExposureModeKey, "manual");
  settings->SetBool(kExposureEnabledKey, true);
  settings->SetFloat(kExposureManualEVKey, 9.7F);
  settings->SetFloat(kExposureCompensationKey, 0.0F);
  settings->SetFloat(kExposureKeyKey, 10.0F);

  settings->SetBool(kTonemappingEnabledKey, true);
  settings->SetString(kToneMapperKey, "aces");

  settings->SetFloat(kAutoExposureSpeedUpKey,
    engine::AutoExposurePassConfig::kDefaultAdaptationSpeedUp);
  settings->SetFloat(kAutoExposureSpeedDownKey,
    engine::AutoExposurePassConfig::kDefaultAdaptationSpeedDown);
  settings->SetFloat(kAutoExposureLowPercentileKey,
    engine::AutoExposurePassConfig::kDefaultLowPercentile);
  settings->SetFloat(kAutoExposureHighPercentileKey,
    engine::AutoExposurePassConfig::kDefaultHighPercentile);
  settings->SetFloat(kAutoExposureMinLogLumKey,
    engine::AutoExposurePassConfig::kDefaultMinLogLuminance);
  settings->SetFloat(kAutoExposureLogLumRangeKey,
    engine::AutoExposurePassConfig::kDefaultLogLuminanceRange);
  settings->SetFloat(kAutoExposureTargetLumKey,
    engine::AutoExposurePassConfig::kDefaultTargetLuminance);
  settings->SetString(kAutoExposureMeteringKey,
    std::string(
      engine::to_string(engine::AutoExposurePassConfig::kDefaultMeteringMode)));

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
  settings->SetFloat(kAutoExposureSpeedUpKey,
    engine::AutoExposurePassConfig::kDefaultAdaptationSpeedUp);
  settings->SetFloat(kAutoExposureSpeedDownKey,
    engine::AutoExposurePassConfig::kDefaultAdaptationSpeedDown);
  settings->SetFloat(kAutoExposureLowPercentileKey,
    engine::AutoExposurePassConfig::kDefaultLowPercentile);
  settings->SetFloat(kAutoExposureHighPercentileKey,
    engine::AutoExposurePassConfig::kDefaultHighPercentile);
  settings->SetFloat(kAutoExposureMinLogLumKey,
    engine::AutoExposurePassConfig::kDefaultMinLogLuminance);
  settings->SetFloat(kAutoExposureLogLumRangeKey,
    engine::AutoExposurePassConfig::kDefaultLogLuminanceRange);
  settings->SetFloat(kAutoExposureTargetLumKey,
    engine::AutoExposurePassConfig::kDefaultTargetLuminance);
  settings->SetString(kAutoExposureMeteringKey,
    std::string(
      engine::to_string(engine::AutoExposurePassConfig::kDefaultMeteringMode)));

  epoch_++;
  Initialize(pipeline_);
}

auto PostProcessSettingsService::UpdateAutoExposureTarget() -> void
{
  if (!pipeline_) {
    return;
  }

  // Calculate effective target luminance by applying exposure compensation
  // bias and exposure key.
  //
  // Standard Exposure Formula: Exposure = Target / Average
  // Final Color = Color * Exposure
  //
  // Compensation (Stops): Target *= 2^Compensation
  // Exposure Key (K): Standard is 12.5.
  // We scale the target by (K / 12.5) to match the Manual mode behavior where
  // Exposure scales linearly with Key.

  const float base_target = GetAutoExposureTargetLuminance();
  const float comp = GetExposureCompensation();
  const float key = GetExposureKey();

  // Combine factors
  const float effective_target = base_target * std::exp2(comp) * (key / 12.5F);

  pipeline_->SetAutoExposureTargetLuminance(effective_target);
}

auto PostProcessSettingsService::ResetAutoExposure(float initial_ev) -> void
{
  if (pipeline_) {
    pipeline_->ResetAutoExposure(initial_ev);
  }
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

} // namespace oxygen::examples::ui
