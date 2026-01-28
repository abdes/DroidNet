//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/DemoShellUi.h"
#include "DemoShell/PanelSideBar.h"
#include "DemoShell/Settings/SettingsService.h"

namespace oxygen::examples {

auto DemoShellUi::Initialize(const DemoShellUiConfig& config) -> void
{
  knobs_ = config.knobs;
  panel_registry_ = config.panel_registry;

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
  if (!knobs_ || !panel_registry_) {
    return;
  }

  panel_side_bar_.Draw();
  side_panel_.Draw(panel_side_bar_.GetWidth());

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
