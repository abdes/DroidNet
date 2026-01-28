//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/AxesWidget.h"
#include "DemoShell/UI/StatsOverlay.h"

namespace oxygen::examples::ui {

//! Configuration for the settings panel.
struct SettingsPanelConfig {
  //! Axes widget to control.
  observer_ptr<AxesWidget> axes_widget { nullptr };

  //! Stats overlay to control.
  observer_ptr<StatsOverlay> stats_overlay { nullptr };
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
  auto LoadSettings() -> void;
  auto SaveAxesVisibleSetting(bool visible) const -> void;
  auto SaveStatsSettings(const StatsOverlayConfig& config) const -> void;

  void DrawStatsSection();

  SettingsPanelConfig config_ {};
  bool settings_loaded_ { false };
};

} // namespace oxygen::examples::ui
