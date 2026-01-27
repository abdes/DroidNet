//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/DemoShellUi.h"

namespace oxygen::examples::demo_shell {

auto DemoShellUi::Initialize(const DemoShellUiConfig& config) -> void
{
  knobs_ = config.knobs;
  panel_registry_ = config.panel_registry;

  toolbar_.Initialize(
    ToolbarConfig { .knobs = knobs_, .panel_registry = panel_registry_ });

  side_panel_.Initialize(SidePanelConfig { .panel_registry = panel_registry_ });
}

auto DemoShellUi::Draw() -> void
{
  if (!knobs_ || !panel_registry_) {
    return;
  }

  toolbar_.Draw();
  side_panel_.Draw(toolbar_.GetHeight());
}

} // namespace oxygen::examples::demo_shell
