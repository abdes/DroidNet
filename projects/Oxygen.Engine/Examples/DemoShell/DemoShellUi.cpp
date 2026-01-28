//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/DemoShellUi.h"
#include "DemoShell/PanelSideBar.h"

namespace oxygen::examples::demo_shell {

auto DemoShellUi::Initialize(const DemoShellUiConfig& config) -> void
{
  knobs_ = config.knobs;
  panel_registry_ = config.panel_registry;

  panel_side_bar_.Initialize(
    PanelSideBarConfig { .panel_registry = panel_registry_ });
  side_panel_.Initialize(SidePanelConfig { .panel_registry = panel_registry_ });
}

auto DemoShellUi::Draw() -> void
{
  if (!knobs_ || !panel_registry_) {
    return;
  }

  panel_side_bar_.Draw();
  side_panel_.Draw(panel_side_bar_.GetWidth());
}

} // namespace oxygen::examples::demo_shell
