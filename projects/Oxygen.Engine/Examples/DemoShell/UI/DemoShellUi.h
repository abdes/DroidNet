//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {

class PanelRegistry;
class UiSettingsService;
class CameraLifecycleService;

namespace ui {

  //! UI shell hosting the side bar and side panel.
  /*!
   Provides a reusable UI layout for demos, consisting of a left-docked
   `PanelSideBar` and a `SidePanel` hosting a single active panel.
  */
  class DemoShellUi {
  public:
    DemoShellUi(observer_ptr<PanelRegistry> panel_registry,
      observer_ptr<CameraLifecycleService> camera_lifecycle,
      observer_ptr<UiSettingsService> ui_settings_service);
    ~DemoShellUi();

    OXYGEN_MAKE_NON_COPYABLE(DemoShellUi)
    OXYGEN_DEFAULT_MOVABLE(DemoShellUi)

    //! Draws the side bar and side panel.
    auto Draw() -> void;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_ {};
  };

} // namespace ui

} // namespace oxygen::examples
