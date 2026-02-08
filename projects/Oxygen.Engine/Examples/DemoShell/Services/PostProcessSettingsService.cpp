//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
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
    const engine::ExposureMode mode, const float manual_ev100,
    const float manual_camera_ev100, const float compensation_ev,
    const float exposure_key, const bool enabled) -> void
  {
    if (!pipeline) {
      return;
    }

    pipeline->SetExposureMode(mode);
    if (!enabled || mode == engine::ExposureMode::kAuto) {
      pipeline->SetExposureValue(1.0F);
      return;
    }

    const float ev100 = (mode == engine::ExposureMode::kManualCamera)
      ? manual_camera_ev100
      : manual_ev100;
    const float exposure
      = (1.0F / 12.5F) * std::exp2(compensation_ev - ev100) * exposure_key;
    pipeline->SetExposureValue(exposure);
  }

  auto ApplyExposureToScene(observer_ptr<scene::Scene> scene,
    const engine::ExposureMode mode, const float manual_ev100,
    const float compensation_ev, const float exposure_key, const bool enabled)
    -> void
  {
    const auto pp = EnsurePostProcessVolume(scene);
    if (!pp) {
      return;
    }

    pp->SetExposureEnabled(enabled);
    pp->SetExposureMode(ToSceneExposureMode(mode));
    pp->SetManualExposureEv100(manual_ev100);
    pp->SetExposureCompensationEv(compensation_ev);
    pp->SetExposureKey(exposure_key);
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

  auto ResolveManualCameraEv100(
    observer_ptr<oxygen::examples::CameraSettingsService> camera_settings)
    -> float
  {
    if (auto* exposure = ResolveActiveCameraExposure(camera_settings)) {
      return exposure->GetEv100();
    }

    const oxygen::scene::CameraExposure fallback {};
    return fallback.GetEv100();
  }
} // namespace

auto PostProcessSettingsService::Initialize(
  observer_ptr<RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;

  // Push initial state
  if (pipeline_) {
    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_),
      GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
    pipeline_->SetToneMapper(
      GetTonemappingEnabled() ? GetToneMapper() : engine::ToneMapper::kNone);
  }

  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEV100(),
    GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
}

auto PostProcessSettingsService::BindCameraSettings(
  observer_ptr<CameraSettingsService> camera_settings) -> void
{
  camera_settings_ = camera_settings;
  epoch_++;

  if (pipeline_) {
    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_),
      GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::BindScene(observer_ptr<scene::Scene> scene)
  -> void
{
  scene_ = scene;
  epoch_++;

  if (pipeline_) {
    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_),
      GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
  }
  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEV100(),
    GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
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

  ApplyExposureToPipeline(pipeline_, GetExposureMode(),
    GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_),
    GetExposureCompensation(), GetExposureKey(), enabled);
  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEV100(),
    GetExposureCompensation(), GetExposureKey(), enabled);
}

auto PostProcessSettingsService::SetExposureMode(engine::ExposureMode mode)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetString(kExposureModeKey, engine::to_string(mode));
  epoch_++;

  ApplyExposureToPipeline(pipeline_, mode, GetManualExposureEV100(),
    ResolveManualCameraEv100(camera_settings_), GetExposureCompensation(),
    GetExposureKey(), GetExposureEnabled());
  ApplyExposureToScene(scene_, mode, GetManualExposureEV100(),
    GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
}

auto PostProcessSettingsService::GetManualExposureEV100() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kExposureManualEV100Key).value_or(9.7F);
}

auto PostProcessSettingsService::SetManualExposureEV100(float ev100) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  ev100 = std::max(ev100, 0.0F);
  settings->SetFloat(kExposureManualEV100Key, ev100);
  epoch_++;

  ApplyExposureToPipeline(pipeline_, GetExposureMode(), ev100,
    ResolveManualCameraEv100(camera_settings_), GetExposureCompensation(),
    GetExposureKey(), GetExposureEnabled());
  ApplyExposureToScene(scene_, GetExposureMode(), ev100,
    GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
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

    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_),
      GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
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

    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_),
      GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
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

    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_),
      GetExposureCompensation(), GetExposureKey(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualCameraEV100() const -> float
{
  return ResolveManualCameraEv100(camera_settings_);
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

  ApplyExposureToPipeline(pipeline_, GetExposureMode(),
    GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_), stops,
    GetExposureKey(), GetExposureEnabled());
  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEV100(),
    stops, GetExposureKey(), GetExposureEnabled());
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

  ApplyExposureToPipeline(pipeline_, GetExposureMode(),
    GetManualExposureEV100(), ResolveManualCameraEv100(camera_settings_),
    GetExposureCompensation(), exposure_key, GetExposureEnabled());
  ApplyExposureToScene(scene_, GetExposureMode(), GetManualExposureEV100(),
    GetExposureCompensation(), exposure_key, GetExposureEnabled());
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

  if (pipeline_ && GetTonemappingEnabled()) {
    pipeline_->SetToneMapper(mode);
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
