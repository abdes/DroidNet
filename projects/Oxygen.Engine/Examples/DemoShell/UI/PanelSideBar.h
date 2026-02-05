//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelRegistry;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class UiSettingsVm;

class PanelSideBar {
public:
  PanelSideBar(observer_ptr<PanelRegistry> panel_registry,
    observer_ptr<UiSettingsVm> ui_settings_vm);
  ~PanelSideBar() = default;

  OXYGEN_MAKE_NON_COPYABLE(PanelSideBar)
  OXYGEN_DEFAULT_MOVABLE(PanelSideBar)

  auto Draw() -> void;

  [[nodiscard]] auto GetWidth() const noexcept -> float;

private:
  observer_ptr<PanelRegistry> panel_registry_;
  observer_ptr<UiSettingsVm> ui_settings_vm_;
};

} // namespace oxygen::examples::ui
