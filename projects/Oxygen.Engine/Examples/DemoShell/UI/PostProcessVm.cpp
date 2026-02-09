//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include "DemoShell/UI/PostProcessVm.h"

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
  manual_ev100_ = service_->GetManualExposureEV100();
  manual_camera_aperture_ = service_->GetManualCameraAperture();
  manual_camera_shutter_rate_ = service_->GetManualCameraShutterRate();
  manual_camera_iso_ = service_->GetManualCameraIso();
  exposure_compensation_ = service_->GetExposureCompensation();
  exposure_key_ = service_->GetExposureKey();

  auto_exposure_speed_up_ = service_->GetAutoExposureAdaptationSpeedUp();
  auto_exposure_speed_down_ = service_->GetAutoExposureAdaptationSpeedDown();
  auto_exposure_low_percentile_ = service_->GetAutoExposureLowPercentile();
  auto_exposure_high_percentile_ = service_->GetAutoExposureHighPercentile();
  auto_exposure_min_log_lum_ = service_->GetAutoExposureMinLogLuminance();
  auto_exposure_log_lum_range_ = service_->GetAutoExposureLogLuminanceRange();
  auto_exposure_target_lum_ = service_->GetAutoExposureTargetLuminance();
  auto_exposure_metering_mode_ = service_->GetAutoExposureMeteringMode();

  tonemapping_enabled_ = service_->GetTonemappingEnabled();
  tonemapping_mode_ = service_->GetToneMapper();
}

auto PostProcessVm::IsStale() const -> bool
{
  return service_ && service_->GetEpoch() != epoch_;
}

// Exposure

auto PostProcessVm::GetExposureEnabled() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return exposure_enabled_;
}

auto PostProcessVm::SetExposureEnabled(bool enabled) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetExposureEnabled(enabled);
    Refresh();
  }
}

auto PostProcessVm::GetExposureMode() -> engine::ExposureMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return exposure_mode_;
}

auto PostProcessVm::SetExposureMode(engine::ExposureMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetExposureMode(mode);
    Refresh();
  }
}

auto PostProcessVm::GetManualExposureEV100() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return manual_ev100_;
}

auto PostProcessVm::SetManualExposureEV100(float ev100) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetManualExposureEV100(std::max(ev100, 0.0F));
    Refresh();
  }
}

auto PostProcessVm::GetManualCameraAperture() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return manual_camera_aperture_;
}

auto PostProcessVm::SetManualCameraAperture(float aperture) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetManualCameraAperture(aperture);
    Refresh();
  }
}

auto PostProcessVm::GetManualCameraShutterRate() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return manual_camera_shutter_rate_;
}

auto PostProcessVm::SetManualCameraShutterRate(float shutter_rate) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetManualCameraShutterRate(shutter_rate);
    Refresh();
  }
}

auto PostProcessVm::GetManualCameraIso() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return manual_camera_iso_;
}

auto PostProcessVm::SetManualCameraIso(float iso) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetManualCameraIso(iso);
    Refresh();
  }
}

auto PostProcessVm::GetManualCameraEV100() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  if (service_) {
    return service_->GetManualCameraEV100();
  }
  return manual_ev100_;
}

auto PostProcessVm::GetExposureCompensation() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return exposure_compensation_;
}

auto PostProcessVm::SetExposureCompensation(float stops) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetExposureCompensation(stops);
    Refresh();
  }
}

auto PostProcessVm::GetExposureKey() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return exposure_key_;
}

auto PostProcessVm::SetExposureKey(float exposure_key) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetExposureKey(exposure_key);
    Refresh();
  }
}

// Auto Exposure

auto PostProcessVm::GetAutoExposureAdaptationSpeedUp() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_speed_up_;
}

auto PostProcessVm::SetAutoExposureAdaptationSpeedUp(float speed) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetAutoExposureAdaptationSpeedUp(speed);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureAdaptationSpeedDown() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_speed_down_;
}

auto PostProcessVm::SetAutoExposureAdaptationSpeedDown(float speed) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetAutoExposureAdaptationSpeedDown(speed);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureLowPercentile() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_low_percentile_;
}

auto PostProcessVm::SetAutoExposureLowPercentile(float percentile) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetAutoExposureLowPercentile(percentile);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureHighPercentile() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_high_percentile_;
}

auto PostProcessVm::SetAutoExposureHighPercentile(float percentile) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetAutoExposureHighPercentile(percentile);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureMinLogLuminance() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_min_log_lum_;
}

auto PostProcessVm::SetAutoExposureMinLogLuminance(float min_log_lum) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetAutoExposureMinLogLuminance(min_log_lum);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureLogLuminanceRange() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_log_lum_range_;
}

auto PostProcessVm::SetAutoExposureLogLuminanceRange(float range) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetAutoExposureLogLuminanceRange(range);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureTargetLuminance() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_target_lum_;
}

auto PostProcessVm::SetAutoExposureTargetLuminance(float target_lum) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetAutoExposureTargetLuminance(target_lum);
    Refresh();
  }
}

auto PostProcessVm::GetAutoExposureMeteringMode() -> engine::MeteringMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return auto_exposure_metering_mode_;
}

auto PostProcessVm::SetAutoExposureMeteringMode(engine::MeteringMode mode)
  -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetAutoExposureMeteringMode(mode);
    Refresh();
  }
}

// Tonemapping

auto PostProcessVm::GetTonemappingEnabled() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return tonemapping_enabled_;
}

auto PostProcessVm::SetTonemappingEnabled(bool enabled) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetTonemappingEnabled(enabled);
    Refresh();
  }
}

auto PostProcessVm::GetToneMapper() -> engine::ToneMapper
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return tonemapping_mode_;
}

auto PostProcessVm::SetToneMapper(engine::ToneMapper mode) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetToneMapper(mode);
    Refresh();
  }
}

auto PostProcessVm::ResetToDefaults() -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->ResetToDefaults();
    Refresh();
  }
}

auto PostProcessVm::ResetAutoExposureDefaults() -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->ResetAutoExposureDefaults();
    Refresh();
  }
}

} // namespace oxygen::examples::ui
