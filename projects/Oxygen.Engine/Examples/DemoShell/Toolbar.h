//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/DemoKnobsViewModel.h"
#include "DemoShell/PanelRegistry.h"

namespace oxygen::examples::demo_shell {

//! Toolbar configuration for the demo shell.
struct ToolbarConfig {
  observer_ptr<DemoKnobsViewModel> knobs { nullptr };
  observer_ptr<PanelRegistry> panel_registry { nullptr };
};

//! Fixed top toolbar for demo controls.
/*!
 Draws a full-width ImGui toolbar containing the panel menu button and common
 demo knobs such as render mode and camera mode.

 ### Key Features

 - **Panel Menu**: Single-entry menu button for panel selection.
 - **Common Knobs**: Render mode, camera mode, axis widget toggle.
 - **Full Width**: Stretches across the entire window.
*/
class Toolbar {
public:
  Toolbar() = default;
  ~Toolbar() = default;

  Toolbar(const Toolbar&) = delete;
  auto operator=(const Toolbar&) -> Toolbar& = delete;
  Toolbar(Toolbar&&) = default;
  auto operator=(Toolbar&&) -> Toolbar& = default;

  //! Initialize the toolbar with its dependencies.
  auto Initialize(const ToolbarConfig& config) -> void;

  //! Draw the toolbar window and its contents.
  auto Draw() -> void;

  //! Returns the last measured toolbar height.
  [[nodiscard]] auto GetHeight() const noexcept -> float;

private:
  auto DrawPanelMenu() -> void;
  auto DrawKnobs() -> void;
  auto DrawStats() -> void;

  ToolbarConfig config_ {};
  float height_ { 36.0F };
};

} // namespace oxygen::examples::demo_shell
