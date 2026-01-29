//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/PanelRegistry.h"

namespace oxygen::examples::ui {

//! Left-docked side panel hosting the active demo panel.
/*!
 Draws a single ImGui window docked to the left side of the main window. The
 panel stretches vertically and can be resized horizontally with a minimum
 width constraint.
*/
class SidePanel {
public:
  SidePanel(observer_ptr<PanelRegistry> panel_registry);
  ~SidePanel() = default;

  OXYGEN_MAKE_NON_COPYABLE(SidePanel)
  OXYGEN_DEFAULT_MOVABLE(SidePanel)

  //! Draws the side panel window and the active panel content.
  //!
  //! The left offset param is the horizontal size of any left-docked UI such
  //! as a `PanelSideBar` so the SidePanel positions itself after it.
  auto Draw(float left_offset) -> void;

private:
  observer_ptr<PanelRegistry> panel_registry_;
  float width_ { 420.0F };
  // Track last active panel name to detect selection changes.
  std::string last_active_panel_name_ {};
  float last_saved_panel_width_ { 0.0F };
};

} // namespace oxygen::examples::ui
