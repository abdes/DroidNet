//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/DemoKnobsViewModel.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/SidePanel.h"
#include "DemoShell/Toolbar.h"

namespace oxygen::examples::demo_shell {

//! Configuration for the DemoShellUi controller.
struct DemoShellUiConfig {
  observer_ptr<DemoKnobsViewModel> knobs { nullptr };
  observer_ptr<PanelRegistry> panel_registry { nullptr };
};

//! UI shell hosting the demo toolbar and side panel.
/*!
 Provides a reusable UI layout for demos, consisting of a fixed top toolbar and
 a left-docked SidePanel hosting a single active panel.
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

  //! Draws the toolbar and side panel.
  auto Draw() -> void;

  //! Returns the fixed toolbar height in pixels.
  [[nodiscard]] static constexpr auto ToolbarHeight() noexcept -> float
  {
    return kToolbarHeight;
  }

private:
  static constexpr float kToolbarHeight = 36.0F;

  observer_ptr<DemoKnobsViewModel> knobs_ { nullptr };
  observer_ptr<PanelRegistry> panel_registry_ { nullptr };
  Toolbar toolbar_ {};
  SidePanel side_panel_ {};
};

} // namespace oxygen::examples::demo_shell
