//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include "DemoShell/PanelRegistry.h"
#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/AxesWidget.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/PanelSideBar.h"
#include "DemoShell/UI/SidePanel.h"
#include "DemoShell/UI/StatsOverlay.h"
#include "DemoShell/UI/UiSettingsPanel.h"
#include "DemoShell/UI/UiSettingsVm.h"

namespace oxygen::examples::ui {

struct DemoShellUi::Impl {
  observer_ptr<PanelRegistry> panel_registry {};
  UiSettingsVm ui_settings_vm;
  observer_ptr<UiSettingsVm> ui_settings_vm_ptr { &ui_settings_vm };
  PanelSideBar side_bar;
  SidePanel side_panel;
  AxesWidget axes_widget;
  StatsOverlay stats_overlay;
  std::shared_ptr<UiSettingsPanel> settings_panel {};

  Impl(observer_ptr<PanelRegistry> registry,
    observer_ptr<CameraLifecycleService> camera_lifecycle,
    observer_ptr<UiSettingsService> ui_settings_service)
    : panel_registry(registry)
    , ui_settings_vm(ui_settings_service, camera_lifecycle)
    , side_bar(panel_registry, ui_settings_vm_ptr)
    , side_panel(panel_registry)
    , axes_widget(ui_settings_vm_ptr)
    , stats_overlay(ui_settings_vm_ptr)
  {
    DCHECK_NOTNULL_F(panel_registry, "expecting valid PanelRegistry");
    DCHECK_NOTNULL_F(ui_settings_service, "expecting valid UiSettingsService");
    settings_panel = std::make_shared<UiSettingsPanel>(ui_settings_vm_ptr);
    const auto result = panel_registry->RegisterPanel(settings_panel);
    if (!result) {
      LOG_F(WARNING, "DemoShellUi: failed to register Settings panel");
    }
  }
};

DemoShellUi::DemoShellUi(observer_ptr<PanelRegistry> panel_registry,
  observer_ptr<CameraLifecycleService> camera_lifecycle,
  observer_ptr<UiSettingsService> ui_settings_service)
  : impl_(std::make_unique<Impl>(
      panel_registry, camera_lifecycle, ui_settings_service))
{
}

DemoShellUi::~DemoShellUi() = default;

auto DemoShellUi::Draw() -> void
{
  impl_->side_bar.Draw();
  impl_->side_panel.Draw(impl_->side_bar.GetWidth());

  // Settings now flow through UiSettingsVm instead of view-owned state.
  impl_->axes_widget.Draw(impl_->ui_settings_vm.GetActiveCamera());
  impl_->stats_overlay.Draw();
}

} // namespace oxygen::examples::ui
