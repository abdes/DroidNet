//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/UI/RenderingVm.h"
#include "DemoShell/Services/RenderingSettingsService.h"

namespace oxygen::examples::ui {

RenderingVm::RenderingVm(observer_ptr<RenderingSettingsService> service,
  observer_ptr<engine::ShaderPassConfig> pass_config)
  : service_(service)
  , pass_config_(pass_config)
{
  Refresh();
}

auto RenderingVm::GetViewMode() -> RenderingViewMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return view_mode_;
}

auto RenderingVm::GetDebugMode() -> engine::ShaderDebugMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return debug_mode_;
}

auto RenderingVm::SetViewMode(RenderingViewMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (view_mode_ == mode) {
    return;
  }

  view_mode_ = mode;
  service_->SetViewMode(FromViewMode(mode));
  epoch_ = service_->GetEpoch();
}

auto RenderingVm::SetDebugMode(engine::ShaderDebugMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (debug_mode_ == mode) {
    return;
  }

  debug_mode_ = mode;
  service_->SetDebugMode(mode);
  epoch_ = service_->GetEpoch();

  // Apply directly to pass config
  if (pass_config_) {
    pass_config_->debug_mode = mode;
  }
}

auto RenderingVm::SetPassConfig(
  observer_ptr<engine::ShaderPassConfig> pass_config) -> void
{
  std::lock_guard lock(mutex_);
  pass_config_ = pass_config;
  // State is now applied every frame by the application module
  // to avoid conflicts between multiple ViewModels.
}

auto RenderingVm::Refresh() -> void
{
  // Assume mutex is already held by caller (GetViewMode/GetDebugMode)
  // or it's called from constructor (which doesn't need it)
  view_mode_ = ToViewMode(service_->GetViewMode());
  debug_mode_ = service_->GetDebugMode();
  epoch_ = service_->GetEpoch();
}

auto RenderingVm::IsStale() const -> bool
{
  // Assume mutex is already held
  return epoch_ != service_->GetEpoch();
}

auto RenderingVm::ToViewMode(RenderingSettingsService::ViewMode mode)
  -> RenderingViewMode
{
  switch (mode) {
  case RenderingSettingsService::ViewMode::kWireframe:
    return RenderingViewMode::kWireframe;
  case RenderingSettingsService::ViewMode::kSolid:
  default:
    return RenderingViewMode::kSolid;
  }
}

auto RenderingVm::FromViewMode(RenderingViewMode mode)
  -> RenderingSettingsService::ViewMode
{
  switch (mode) {
  case RenderingViewMode::kWireframe:
    return RenderingSettingsService::ViewMode::kWireframe;
  case RenderingViewMode::kSolid:
  default:
    return RenderingSettingsService::ViewMode::kSolid;
  }
}

} // namespace oxygen::examples::ui
