//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include "DemoShell/UI/DemoPanel.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class CameraVm;
class UiSettingsVm;

//! Camera control mode
enum class CameraControlMode { kOrbit, kFly, kDrone };

//! Camera control panel with mode switching and debugging
/*!
 Switches between orbit, fly and drone control, edits their settings and the
 active camera's projection, and shows the camera pose and the axes. Every
 control runs a `demo.camera.*` command.

 @see CameraVm
 */
class CameraControlPanel final : public DemoPanel {
public:
  CameraControlPanel(observer_ptr<CameraVm> vm,
    observer_ptr<UiSettingsVm> ui_settings,
    observer_ptr<PanelCommands> commands);
  ~CameraControlPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(CameraControlPanel)
  OXYGEN_MAKE_NON_MOVABLE(CameraControlPanel)

  //! Draws the panel content without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  void DrawCameraModeTab();
  void DrawOrientationTab();
  void DrawCameraPoseInfo();
  void DrawProjectionSettings();
  void DrawDroneMinimap();

  observer_ptr<CameraVm> vm_;
  observer_ptr<UiSettingsVm> ui_settings_;
  observer_ptr<PanelCommands> commands_;
};

} // namespace oxygen::examples::ui
