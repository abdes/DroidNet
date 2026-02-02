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
  if (!service_)
    return;

  epoch_ = service_->GetEpoch();
  compositing_enabled_ = service_->GetCompositingEnabled();
  compositing_alpha_ = service_->GetCompositingAlpha();
  exposure_mode_ = service_->GetExposureMode();
  manual_ev100_ = service_->GetManualExposureEV100();
  exposure_compensation_ = service_->GetExposureCompensation();
  tonemapping_enabled_ = service_->GetTonemappingEnabled();
  tonemapping_mode_ = service_->GetToneMapper();
}

auto PostProcessVm::IsStale() const -> bool
{
  return service_ && service_->GetEpoch() != epoch_;
}

// Compositing

auto PostProcessVm::GetCompositingEnabled() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale())
    Refresh();
  return compositing_enabled_;
}

auto PostProcessVm::SetCompositingEnabled(bool enabled) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetCompositingEnabled(enabled);
    Refresh();
  }
}

auto PostProcessVm::GetCompositingAlpha() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale())
    Refresh();
  return compositing_alpha_;
}

auto PostProcessVm::SetCompositingAlpha(float alpha) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetCompositingAlpha(alpha);
    Refresh();
  }
}

// Exposure

auto PostProcessVm::GetExposureMode() -> engine::ExposureMode
{
  std::lock_guard lock(mutex_);
  if (IsStale())
    Refresh();
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
  if (IsStale())
    Refresh();
  return manual_ev100_;
}

auto PostProcessVm::SetManualExposureEV100(float ev100) -> void
{
  std::lock_guard lock(mutex_);
  if (service_) {
    service_->SetManualExposureEV100(ev100);
    Refresh();
  }
}

auto PostProcessVm::GetExposureCompensation() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale())
    Refresh();
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

// Tonemapping

auto PostProcessVm::GetTonemappingEnabled() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale())
    Refresh();
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
  if (IsStale())
    Refresh();
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

} // namespace oxygen::examples::ui
