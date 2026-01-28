//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/DemoKnobsViewModel.h"

namespace oxygen::examples::render_scene::ui {

//! Configuration for the settings panel.
struct SettingsPanelConfig {
  //! Shared demo knob state for settings toggles.
  observer_ptr<demo_shell::DemoKnobsViewModel> demo_knobs { nullptr };
};

//! Settings panel for UI visibility and stats toggles.
/*!
 Provides axis visibility control and a stats section for FPS and frame
 timing display.
*/
class SettingsPanel {
public:
  //! Initialize the panel with configuration.
  void Initialize(const SettingsPanelConfig& config);

  //! Update configuration.
  void UpdateConfig(const SettingsPanelConfig& config);

  //! Draw the panel contents without creating a window.
  void DrawContents();

private:
  void DrawStatsSection();

  SettingsPanelConfig config_ {};
};

} // namespace oxygen::examples::render_scene::ui
