//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples {
class CameraSettingsService;
class UiSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

//! View model of the DemoShell layout settings: axes visibility and the
//! active panel.
/*!
 Caches the settings of `UiSettingsService`, refreshing when the service
 epoch changes. The stats overlay belongs to the host toolbar.

@see oxygen::examples::UiSettingsService
*/
class UiSettingsVm {
public:
  //! Creates a view model backed by the provided settings service.
  explicit UiSettingsVm(observer_ptr<UiSettingsService> service,
    observer_ptr<CameraSettingsService> camera_settings = nullptr);

  //! Returns the cached axes visibility.
  [[nodiscard]] auto GetAxesVisible() -> bool;

  //! Returns the cached active panel name (empty if none).
  [[nodiscard]] auto GetActivePanelName() -> std::optional<std::string>;

  //! Returns the active camera node (null when unavailable).
  [[nodiscard]] auto GetActiveCamera() const -> observer_ptr<scene::SceneNode>;

  //! Sets axes visibility and forwards changes to the service.
  auto SetAxesVisible(bool visible) -> void;

  //! Sets the active panel name and forwards changes to the service.
  auto SetActivePanelName(std::optional<std::string> panel_name) -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  mutable std::mutex mutex_;
  observer_ptr<UiSettingsService> service_;
  observer_ptr<CameraSettingsService> camera_settings_;
  std::uint64_t epoch_ { 0 };
  bool axes_visible_ { true };
  std::optional<std::string> active_panel_name_;
};

} // namespace oxygen::examples::ui
