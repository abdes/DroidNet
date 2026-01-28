//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/DemoKnobsViewModel.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/PanelSideBar.h"
#include "DemoShell/SidePanel.h"

namespace oxygen::examples {

//! Configuration for the DemoShellUi controller.
struct DemoShellUiConfig {
  observer_ptr<DemoKnobsViewModel> knobs { nullptr };
  observer_ptr<PanelRegistry> panel_registry { nullptr };
};

//! UI shell hosting the side bar and side panel.
/*!
 Provides a reusable UI layout for demos, consisting of a left-docked
 `PanelSideBar` and a `SidePanel` hosting a single active panel.
*/
class DemoShellUi {
public:
  DemoShellUi() = default;
  ~DemoShellUi() = default;

  DemoShellUi(const DemoShellUi&) = delete;
  auto operator=(const DemoShellUi&) -> DemoShellUi& = delete;
  DemoShellUi(DemoShellUi&&) = default;
  auto operator=(DemoShellUi&&) -> DemoShellUi& = default;

  //! Initialize the UI shell with required dependencies.
  auto Initialize(const DemoShellUiConfig& config) -> void;

  //! Draws the side bar and side panel.
  auto Draw() -> void;

private:
  observer_ptr<DemoKnobsViewModel> knobs_ { nullptr };
  observer_ptr<PanelRegistry> panel_registry_ { nullptr };
  PanelSideBar panel_side_bar_ {};
  SidePanel side_panel_ {};
  std::string last_active_panel_name_ {};
  std::optional<std::string> pending_active_panel_ {};
};

} // namespace oxygen::examples
