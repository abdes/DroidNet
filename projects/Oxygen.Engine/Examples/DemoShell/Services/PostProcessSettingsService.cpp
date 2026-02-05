//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/PostProcessSettingsService.h"

#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/SettingsService.h"

#include <cmath>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::examples::ui {

namespace {
  auto ConvertEv100ToExposure(const float ev100, const float compensation_ev)
    -> float
  {
    return std::exp2(compensation_ev - ev100);
  }

  auto ApplyExposureToPipeline(observer_ptr<RenderingPipeline> pipeline,
    const engine::ExposureMode mode, const float manual_ev100,
    const float manual_camera_ev100, const float compensation_ev,
    const bool enabled) -> void
  {
    if (!pipeline) {
      return;
    }

    if (!enabled) {
      pipeline->SetExposureMode(engine::ExposureMode::kManual);
      pipeline->SetExposureValue(1.0F);
      return;
    }

    pipeline->SetExposureMode(mode);
    if (mode == engine::ExposureMode::kManual) {
      pipeline->SetExposureValue(
        ConvertEv100ToExposure(manual_ev100, compensation_ev));
      return;
    }

    if (mode == engine::ExposureMode::kManualCamera) {
      pipeline->SetExposureValue(
        ConvertEv100ToExposure(manual_camera_ev100, compensation_ev));
    }
  }

  auto ResolveActiveCameraExposure(
    observer_ptr<oxygen::examples::CameraLifecycleService> camera_lifecycle)
    -> oxygen::scene::CameraExposure*
  {
    if (!camera_lifecycle) {
      return nullptr;
    }

    auto& active_camera = camera_lifecycle->GetActiveCamera();
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
    observer_ptr<oxygen::examples::CameraLifecycleService> camera_lifecycle)
    -> float
  {
    if (auto* exposure = ResolveActiveCameraExposure(camera_lifecycle)) {
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
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_lifecycle_),
      GetExposureCompensation(), GetExposureEnabled());
    pipeline_->SetToneMapper(
      GetTonemappingEnabled() ? GetToneMapper() : engine::ToneMapper::kNone);
  }
}

auto PostProcessSettingsService::BindCameraLifecycle(
  observer_ptr<CameraLifecycleService> camera_lifecycle) -> void
{
  camera_lifecycle_ = camera_lifecycle;
  epoch_++;

  if (pipeline_) {
    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_lifecycle_),
      GetExposureCompensation(), GetExposureEnabled());
  }
}

// Exposure

auto PostProcessSettingsService::GetExposureMode() const -> engine::ExposureMode
{
  const auto settings = ResolveSettings();
  if (settings) {
    auto val = settings->GetString(kExposureModeKey).value_or("manual");
    if (val == "auto")
      return engine::ExposureMode::kAuto;
    if (val == "manual_camera")
      return engine::ExposureMode::kManualCamera;
  }
  return engine::ExposureMode::kManual;
}

auto PostProcessSettingsService::GetExposureEnabled() const -> bool
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetBool(kExposureEnabledKey).value_or(true);
  }
  return true;
}

auto PostProcessSettingsService::SetExposureEnabled(bool enabled) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetBool(kExposureEnabledKey, enabled);
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_lifecycle_),
      GetExposureCompensation(), enabled);
  }
}

auto PostProcessSettingsService::SetExposureMode(engine::ExposureMode mode)
  -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetString(kExposureModeKey, engine::to_string(mode));
    epoch_++;

    ApplyExposureToPipeline(pipeline_, mode, GetManualExposureEV100(),
      ResolveManualCameraEv100(camera_lifecycle_), GetExposureCompensation(),
      GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualExposureEV100() const -> float
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetFloat(kExposureManualEV100Key).value_or(9.7F);
  }
  return 9.7F;
}

auto PostProcessSettingsService::SetManualExposureEV100(float ev100) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetFloat(kExposureManualEV100Key, ev100);
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(), ev100,
      ResolveManualCameraEv100(camera_lifecycle_), GetExposureCompensation(),
      GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualCameraAperture() const -> float
{
  if (const auto* exposure = ResolveActiveCameraExposure(camera_lifecycle_)) {
    return exposure->aperture_f;
  }
  return 11.0F;
}

auto PostProcessSettingsService::SetManualCameraAperture(float aperture) -> void
{
  if (auto* exposure = ResolveActiveCameraExposure(camera_lifecycle_)) {
    exposure->aperture_f = aperture;
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_lifecycle_),
      GetExposureCompensation(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualCameraShutterRate() const -> float
{
  if (const auto* exposure = ResolveActiveCameraExposure(camera_lifecycle_)) {
    return exposure->shutter_rate;
  }
  return 125.0F;
}

auto PostProcessSettingsService::SetManualCameraShutterRate(float shutter_rate)
  -> void
{
  if (auto* exposure = ResolveActiveCameraExposure(camera_lifecycle_)) {
    exposure->shutter_rate = shutter_rate;
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_lifecycle_),
      GetExposureCompensation(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualCameraIso() const -> float
{
  if (const auto* exposure = ResolveActiveCameraExposure(camera_lifecycle_)) {
    return exposure->iso;
  }
  return 100.0F;
}

auto PostProcessSettingsService::SetManualCameraIso(float iso) -> void
{
  if (auto* exposure = ResolveActiveCameraExposure(camera_lifecycle_)) {
    exposure->iso = iso;
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_lifecycle_),
      GetExposureCompensation(), GetExposureEnabled());
  }
}

auto PostProcessSettingsService::GetManualCameraEV100() const -> float
{
  return ResolveManualCameraEv100(camera_lifecycle_);
}

auto PostProcessSettingsService::GetExposureCompensation() const -> float
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetFloat(kExposureCompensationKey).value_or(0.0F);
  }
  return 0.0F;
}

auto PostProcessSettingsService::SetExposureCompensation(float stops) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetFloat(kExposureCompensationKey, stops);
    epoch_++;

    ApplyExposureToPipeline(pipeline_, GetExposureMode(),
      GetManualExposureEV100(), ResolveManualCameraEv100(camera_lifecycle_),
      stops, GetExposureEnabled());
  }
}

// Tonemapping

auto PostProcessSettingsService::GetTonemappingEnabled() const -> bool
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetBool(kTonemappingEnabledKey).value_or(true);
  }
  return true;
}

auto PostProcessSettingsService::SetTonemappingEnabled(bool enabled) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetBool(kTonemappingEnabledKey, enabled);
    epoch_++;

    if (pipeline_) {
      pipeline_->SetToneMapper(
        enabled ? GetToneMapper() : engine::ToneMapper::kNone);
    }
  }
}

auto PostProcessSettingsService::GetToneMapper() const -> engine::ToneMapper
{
  const auto settings = ResolveSettings();
  if (settings) {
    auto val = settings->GetString(kToneMapperKey).value_or("aces");
    if (val == "reinhard")
      return engine::ToneMapper::kReinhard;
    if (val == "aces")
      return engine::ToneMapper::kAcesFitted;
    if (val == "filmic")
      return engine::ToneMapper::kFilmic;
    if (val == "none")
      return engine::ToneMapper::kNone;
  }
  return engine::ToneMapper::kAcesFitted;
}

auto PostProcessSettingsService::SetToneMapper(engine::ToneMapper mode) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetString(kToneMapperKey, engine::to_string(mode));
    epoch_++;

    if (pipeline_ && GetTonemappingEnabled()) {
      pipeline_->SetToneMapper(mode);
    }
  }
}

auto PostProcessSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  if (camera_lifecycle_) {
    const auto& camera = camera_lifecycle_->GetActiveCamera();
    const std::string camera_id
      = camera.IsAlive() ? camera.GetName() : std::string {};
    if (camera_id != last_camera_id_) {
      last_camera_id_ = camera_id;
      epoch_.fetch_add(1, std::memory_order_acq_rel);
    }
  }
  return epoch_.load(std::memory_order_acquire);
}

auto PostProcessSettingsService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::Default();
}

} // namespace oxygen::examples::ui
