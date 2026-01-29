//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/StatsOverlayConfig.h"

namespace oxygen::examples {

class SettingsService;

//! Settings persistence for UI visibility and stats overlays.
/*!
 Owns the UI-facing settings for axes visibility and stats overlays, delegating
 persistence to `SettingsService` and exposing an epoch for cache invalidation.

### Key Features

- **Passive state**: Reads and writes via SettingsService without caching.
- **Epoch tracking**: Increments on each effective change.
- **Testable**: Virtual getters and setters for overrides in tests.

@see SettingsService
*/
class UiSettingsService {
public:
  UiSettingsService() = default;
  virtual ~UiSettingsService() = default;

  OXYGEN_MAKE_NON_COPYABLE(UiSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(UiSettingsService)

  //! Returns whether the axes widget is visible.
  [[nodiscard]] virtual auto GetAxesVisible() const -> bool;

  //! Sets axes widget visibility.
  virtual auto SetAxesVisible(bool visible) -> void;

  //! Returns the current stats overlay configuration.
  [[nodiscard]] virtual auto GetStatsConfig() const -> ui::StatsOverlayConfig;

  //! Sets FPS stats visibility.
  virtual auto SetStatsShowFps(bool visible) -> void;
  //! Sets frame timing detail visibility.
  virtual auto SetStatsShowFrameTimingDetail(bool visible) -> void;

  //! Returns the persisted active panel name (empty if none).
  [[nodiscard]] virtual auto GetActivePanelName() const
    -> std::optional<std::string>;

  //! Persists the active panel name (empty when no panel is active).
  virtual auto SetActivePanelName(std::optional<std::string> panel_name)
    -> void;

  //! Returns the current settings epoch.
  [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

protected:
  //! Returns the settings service used for persistence.
  [[nodiscard]] virtual auto ResolveSettings() const noexcept
    -> observer_ptr<SettingsService>;

private:
  auto SetBoolSetting(std::string_view key, bool value, bool default_value)
    -> void;

  static constexpr std::string_view kAxesVisibleKey = "ui.axes.visible";
  static constexpr std::string_view kStatsShowFpsKey = "ui.stats.show_fps";
  static constexpr std::string_view kStatsShowDetailKey
    = "ui.stats.show_frame_timing_detail";
  static constexpr std::string_view kActivePanelKey = "demo_shell.active_panel";
  static constexpr bool kDefaultAxesVisible = true;

  mutable std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples
