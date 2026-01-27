//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/PanelRegistry.h"

namespace oxygen::examples::demo_shell {

//! Side panel configuration for the demo shell.
struct SidePanelConfig {
  observer_ptr<PanelRegistry> panel_registry { nullptr };
};

//! Left-docked side panel hosting the active demo panel.
/*!
 Draws a single ImGui window docked to the left side of the main window. The
 panel stretches vertically and can be resized horizontally with a minimum
 width constraint.
*/
class SidePanel {
public:
  SidePanel() = default;
  ~SidePanel() = default;

  SidePanel(const SidePanel&) = delete;
  auto operator=(const SidePanel&) -> SidePanel& = delete;
  SidePanel(SidePanel&&) = default;
  auto operator=(SidePanel&&) -> SidePanel& = default;

  //! Initialize the side panel with its dependencies.
  auto Initialize(const SidePanelConfig& config) -> void;

  //! Draws the side panel window and the active panel content.
  auto Draw(float toolbar_height) -> void;

private:
  SidePanelConfig config_ {};
  float width_ { 420.0F };
};

} // namespace oxygen::examples::demo_shell
