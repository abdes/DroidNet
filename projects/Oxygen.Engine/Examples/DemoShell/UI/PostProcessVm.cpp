//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>

#include "DemoShell/Runtime/SceneActivationPolicy.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/UI/PostProcessVm.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Vortex/Types/ExposureSettingsStatus.h>

namespace oxygen::examples::ui {

PostProcessVm::PostProcessVm(observer_ptr<PostProcessSettingsService> service)
  : service_(service)
{
  DCHECK_NOTNULL_F(service, "PostProcessVm requires a service");
  Refresh();
}

auto PostProcessVm::Refresh() -> void
{
  if (!service_) {
    return;
  }

  epoch_ = service_->GetEpoch();
  exposure_enabled_ = service_->GetExposureEnabled();
  exposure_mode_ = service_->GetExposureMode();
  manual_ev_ = service_->GetManualExposureEv();
  manual_camera_aperture_ = service_->GetManualCameraAperture();
  manual_camera_shutter_rate_ = service_->GetManualCameraShutterRate();
  manual_camera_iso_ = service_->GetManualCameraIso();
  exposure_compensation_ = service_->GetExposureCompensation();
  exposure_key_ = service_->GetExposureKey();

  auto_exposure_speed_up_ = service_->GetAutoExposureAdaptationSpeedUp();
  auto_exposure_speed_down_ = service_->GetAutoExposureAdaptationSpeedDown();
  auto_exposure_low_percentile_ = service_->GetAutoExposureLowPercentile();
  auto_exposure_high_percentile_ = service_->GetAutoExposureHighPercentile();
  auto_exposure_min_ev_ = service_->GetAutoExposureMinEv();
  auto_exposure_max_ev_ = service_->GetAutoExposureMaxEv();
  auto_exposure_min_log_lum_ = service_->GetAutoExposureMinLogLuminance();
  auto_exposure_log_lum_range_ = service_->GetAutoExposureLogLuminanceRange();
  auto_exposure_target_lum_ = service_->GetAutoExposureTargetLuminance();
  auto_exposure_spot_radius_ = service_->GetAutoExposureSpotMeterRadius();
  auto_exposure_metering_mode_ = service_->GetAutoExposureMeteringMode();

  tonemapping_enabled_ = service_->GetTonemappingEnabled();
  tonemapping_mode_ = service_->GetToneMapper();
  gamma_ = service_->GetGamma();
}

auto PostProcessVm::IsStale() const -> bool
{
  return service_ && service_->GetEpoch() != epoch_;
}

auto PostProcessVm::GetExposureSettings() -> scene::ExposureSettings
{
  std::scoped_lock lock(mutex_);
  return service_->GetExposureSettings();
}
auto PostProcessVm::TrySetExposureSettings(
  const scene::ExposureSettings& settings) -> bool
{
  std::scoped_lock lock(mutex_);
  const bool accepted = service_->TrySetExposureSettings(settings);
  Refresh();
  return accepted;
}
auto PostProcessVm::SetExposureCompensationCurve(
  const std::span<const scene::ExposureCompensationKey> keys) -> bool
{
  std::scoped_lock lock(mutex_);
  const bool accepted = service_->SetExposureCompensationCurve(keys);
  Refresh();
  return accepted;
}
auto PostProcessVm::SetAutoExposureBlackInfluence(const float value) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetAutoExposureBlackInfluence(value);
  Refresh();
}
auto PostProcessVm::SetAutoExposureTransitionDistance(const float value) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetAutoExposureTransitionDistance(value);
  Refresh();
}
auto PostProcessVm::GetExposureStatus()
  -> std::optional<vortex::ExposureSettingsStatus>
{
  std::scoped_lock lock(mutex_);
  return service_->GetExposureStatus();
}
auto PostProcessVm::GetValidationError() -> std::string
{
  std::scoped_lock lock(mutex_);
  return std::string(service_->GetValidationError());
}
auto PostProcessVm::GetSceneActivationPolicy() -> SceneActivationPolicy
{
  return service_->GetSceneActivationPolicy();
}
auto PostProcessVm::GetEpoch() -> std::uint64_t { return service_->GetEpoch(); }
auto PostProcessVm::GetSceneRevision() -> std::uint64_t
{
  return service_->GetSceneRevision();
}
auto PostProcessVm::HasActiveCamera() -> bool
{
  return service_->HasActiveCamera();
}
auto PostProcessVm::HasSceneMeteringMask() -> bool
{
  return service_->HasSceneMeteringMask();
}
auto PostProcessVm::GetUseSceneMeteringMask() -> bool
{
  return service_->GetUseSceneMeteringMask();
}
auto PostProcessVm::SetUseSceneMeteringMask(const bool enabled) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetUseSceneMeteringMask(enabled);
  Refresh();
}
auto PostProcessVm::SetAutoExposureRange(const ExposureRange bounds) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetAutoExposureRange(bounds);
  Refresh();
}
auto PostProcessVm::SetAutoExposurePercentiles(const ExposureRange bounds)
  -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetAutoExposurePercentiles(bounds);
  Refresh();
}
auto PostProcessVm::SetAutoExposureHistogramWindow(const ExposureRange bounds)
  -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetAutoExposureHistogramWindow(bounds);
  Refresh();
}

// Exposure

auto PostProcessVm::GetExposureEnabled() -> bool
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return exposure_enabled_;
}

auto PostProcessVm::SetExposureEnabled(bool enabled) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetExposureEnabled(enabled);
    Refresh();
  }
}

auto PostProcessVm::GetExposureMode() -> engine::ExposureMode
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return exposure_mode_;
}

auto PostProcessVm::SetExposureMode(engine::ExposureMode mode) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetExposureMode(mode);
    Refresh();
  }
}

auto PostProcessVm::GetManualExposureEv() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return manual_ev_;
}

auto PostProcessVm::SetManualExposureEv(float ev) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetManualExposureEv(ev);
    Refresh();
  }
}

auto PostProcessVm::GetManualCameraAperture() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return manual_camera_aperture_;
}

auto PostProcessVm::SetManualCameraAperture(float aperture) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetManualCameraAperture(aperture);
    Refresh();
  }
}

auto PostProcessVm::GetManualCameraShutterRate() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return manual_camera_shutter_rate_;
}

auto PostProcessVm::SetManualCameraShutterRate(float shutter_rate) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetManualCameraShutterRate(shutter_rate);
    Refresh();
  }
}

auto PostProcessVm::GetManualCameraIso() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return manual_camera_iso_;
}

auto PostProcessVm::SetManualCameraIso(float iso) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetManualCameraIso(iso);
    Refresh();
  }
}

auto PostProcessVm::GetManualCameraEv() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  if (service_) {
    return service_->GetManualCameraEv();
  }
  return manual_ev_;
}

auto PostProcessVm::GetExposureCompensation() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return exposure_compensation_;
}

auto PostProcessVm::SetExposureCompensation(float stops) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetExposureCompensation(stops);
    Refresh();
  }
}

auto PostProcessVm::GetExposureKey() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return exposure_key_;
}

auto PostProcessVm::SetExposureKey(float exposure_key) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetExposureKey(exposure_key);
    Refresh();
  }
}

// Auto Exposure

auto PostProcessVm::GetAutoExposureAdaptationSpeedUp() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_speed_up_;
}

auto PostProcessVm::SetAutoExposureAdaptationSpeedUp(float speed) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureAdaptationSpeedUp(speed);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureAdaptationSpeedDown() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_speed_down_;
}

auto PostProcessVm::SetAutoExposureAdaptationSpeedDown(float speed) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureAdaptationSpeedDown(speed);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureLowPercentile() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_low_percentile_;
}

auto PostProcessVm::SetAutoExposureLowPercentile(float percentile) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureLowPercentile(percentile);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureHighPercentile() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_high_percentile_;
}

auto PostProcessVm::SetAutoExposureHighPercentile(float percentile) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureHighPercentile(percentile);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureMinEv() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_min_ev_;
}

auto PostProcessVm::SetAutoExposureMinEv(float min_ev) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureMinEv(min_ev);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureMaxEv() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_max_ev_;
}

auto PostProcessVm::SetAutoExposureMaxEv(float max_ev) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureMaxEv(max_ev);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureMinLogLuminance() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_min_log_lum_;
}

auto PostProcessVm::SetAutoExposureMinLogLuminance(float min_log_lum) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureMinLogLuminance(min_log_lum);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureLogLuminanceRange() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_log_lum_range_;
}

auto PostProcessVm::SetAutoExposureLogLuminanceRange(float range) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureLogLuminanceRange(range);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureTargetLuminance() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_target_lum_;
}

auto PostProcessVm::SetAutoExposureTargetLuminance(float target_lum) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureTargetLuminance(target_lum);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureSpotMeterRadius() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_spot_radius_;
}

auto PostProcessVm::SetAutoExposureSpotMeterRadius(float radius) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureSpotMeterRadius(radius);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureMeteringMode() -> engine::MeteringMode
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_metering_mode_;
}

auto PostProcessVm::SetAutoExposureMeteringMode(engine::MeteringMode mode)
  -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetAutoExposureMeteringMode(mode);
    Refresh();
  }
}

// Tonemapping

auto PostProcessVm::GetTonemappingEnabled() -> bool
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return tonemapping_enabled_;
}

auto PostProcessVm::SetTonemappingEnabled(bool enabled) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetTonemappingEnabled(enabled);
    Refresh();
  }
}

auto PostProcessVm::GetToneMapper() -> engine::ToneMapper
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return tonemapping_mode_;
}

auto PostProcessVm::SetToneMapper(engine::ToneMapper mode) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetToneMapper(mode);
    Refresh();
  }
}

auto PostProcessVm::GetGamma() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return gamma_;
}

auto PostProcessVm::SetGamma(float gamma) -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->SetGamma(gamma);
    Refresh();
  }
}

auto PostProcessVm::ResetToDefaults() -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->ResetToDefaults();
    Refresh();
  }
}

auto PostProcessVm::ResetAutoExposureDefaults() -> void
{
  std::scoped_lock lock(mutex_);
  if (service_) {
    service_->ResetAutoExposureDefaults();
    Refresh();
  }
}

} // namespace oxygen::examples::ui
