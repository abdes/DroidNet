//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/UiSettingsVm.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

UiSettingsVm::UiSettingsVm(observer_ptr<UiSettingsService> service,
  observer_ptr<CameraSettingsService> camera_settings)
  : service_(service)
  , camera_settings_(camera_settings)
{
  Refresh();
}

auto UiSettingsVm::GetAxesVisible() -> bool
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return axes_visible_;
}

auto UiSettingsVm::GetActivePanelName() -> std::optional<std::string>
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return active_panel_name_;
}

auto UiSettingsVm::GetActiveCamera() const -> observer_ptr<scene::SceneNode>
{
  if (!camera_settings_) {
    return nullptr;
  }

  return observer_ptr { &camera_settings_->GetActiveCamera() };
}

auto UiSettingsVm::SetAxesVisible(const bool visible) -> void
{
  std::scoped_lock lock(mutex_);
  if (axes_visible_ == visible) {
    return;
  }

  axes_visible_ = visible;
  service_->SetAxesVisible(visible);
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::SetActivePanelName(std::optional<std::string> panel_name)
  -> void
{
  std::scoped_lock lock(mutex_);
  if (panel_name.has_value()) {
    DCHECK_F(!panel_name->empty(), "expecting non-empty panel names");
  }
  if (active_panel_name_ == panel_name) {
    return;
  }

  active_panel_name_ = panel_name;
  service_->SetActivePanelName(std::move(panel_name));
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::Refresh() -> void
{
  axes_visible_ = service_->GetAxesVisible();
  active_panel_name_ = service_->GetActivePanelName();
  DCHECK_F(!active_panel_name_.has_value() || !active_panel_name_->empty(),
    "expecting non-empty panel names");
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::IsStale() const -> bool
{
  return epoch_ != service_->GetEpoch();
}

} // namespace oxygen::examples::ui
