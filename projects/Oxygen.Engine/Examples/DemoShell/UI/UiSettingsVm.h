//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/UI/StatsOverlayConfig.h"

namespace oxygen::examples {
class CameraLifecycleService;
class UiSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

//! View model for UI settings panel state.
/*!
 Caches UI settings retrieved from `UiSettingsService`, invalidating the cache
 based on the service epoch and applying UI changes back to the service.

### Key Features

- **Epoch-driven refresh**: Reacquires state when stale.
- **Immediate persistence**: Setters forward changes to the service.
- **Dirty tracking**: Records user edits per frame.

@see oxygen::examples::UiSettingsService
*/
class UiSettingsVm {
public:
  //! Creates a view model backed by the provided settings service.
  explicit UiSettingsVm(observer_ptr<UiSettingsService> service,
    observer_ptr<oxygen::examples::CameraLifecycleService> camera_lifecycle
    = nullptr);

  //! Returns the cached axes visibility.
  [[nodiscard]] auto GetAxesVisible() -> bool;

  //! Returns the cached stats overlay configuration.
  [[nodiscard]] auto GetStatsConfig() -> StatsOverlayConfig;

  //! Returns the cached active panel name (empty if none).
  [[nodiscard]] auto GetActivePanelName() -> std::optional<std::string>;

  //! Returns the active camera node (null when unavailable).
  [[nodiscard]] auto GetActiveCamera() const
    -> observer_ptr<oxygen::scene::SceneNode>;

  //! Sets axes visibility and forwards changes to the service.
  auto SetAxesVisible(bool visible) -> void;

  //! Sets FPS stats visibility and forwards changes to the service.
  auto SetStatsShowFps(bool visible) -> void;

  //! Sets frame timing detail visibility and forwards changes to the service.
  auto SetStatsShowFrameTimingDetail(bool visible) -> void;

  //! Sets the active panel name and forwards changes to the service.
  auto SetActivePanelName(std::optional<std::string> panel_name) -> void;

  //! Returns whether axes visibility was edited since the last refresh.
  [[nodiscard]] auto IsAxesDirty() const noexcept -> bool
  {
    return axes_dirty_;
  }

  //! Returns whether stats configuration was edited since the last refresh.
  [[nodiscard]] auto IsStatsDirty() const noexcept -> bool
  {
    return stats_dirty_;
  }

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  observer_ptr<UiSettingsService> service_;
  observer_ptr<CameraLifecycleService> camera_lifecycle_;
  std::uint64_t epoch_ { 0 };
  bool axes_visible_ { true };
  StatsOverlayConfig stats_config_ {};
  std::optional<std::string> active_panel_name_ {};
  bool axes_dirty_ { false };
  bool stats_dirty_ { false };
  bool active_panel_dirty_ { false };
};

} // namespace oxygen::examples::ui
