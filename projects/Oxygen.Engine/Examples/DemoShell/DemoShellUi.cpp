//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/DemoShellUi.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/PanelSideBar.h"

namespace oxygen::examples {

auto DemoShellUi::Initialize(const DemoShellUiConfig& config) -> void
{
  panel_registry_ = config.panel_registry;
  active_camera_ = config.active_camera;

  panel_side_bar_.Initialize(
    PanelSideBarConfig { .panel_registry = panel_registry_ });
  side_panel_.Initialize(SidePanelConfig { .panel_registry = panel_registry_ });

  if (panel_registry_) {
    const auto settings = SettingsService::Default();
    if (settings) {
      pending_active_panel_ = settings->GetString("demo_shell.active_panel");
    }
    last_active_panel_name_
      = std::string(panel_registry_->GetActivePanelName());
  }
}

auto DemoShellUi::Draw() -> void
{
  if (!panel_registry_) {
    return;
  }

  panel_side_bar_.Draw();
  side_panel_.Draw(panel_side_bar_.GetWidth());

  axes_widget_.Draw(active_camera_);
  stats_overlay_.Draw();

  if (pending_active_panel_.has_value()) {
    if (pending_active_panel_->empty()) {
      panel_registry_->ClearActivePanel();
    } else {
      (void)panel_registry_->SetActivePanelByName(*pending_active_panel_);
    }
    pending_active_panel_.reset();
    last_active_panel_name_
      = std::string(panel_registry_->GetActivePanelName());
  }

  const std::string current_active
    = std::string(panel_registry_->GetActivePanelName());
  if (current_active != last_active_panel_name_) {
    last_active_panel_name_ = current_active;
    if (const auto settings = SettingsService::Default()) {
      settings->SetString("demo_shell.active_panel", last_active_panel_name_);
      settings->Save();
    }
  }
}

} // namespace oxygen::examples
