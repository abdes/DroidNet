//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"

namespace oxygen::examples::ui {

class CameraVm;

//! Camera control mode
enum class CameraControlMode { kOrbit, kFly, kDrone };

//! Camera control panel with mode switching and debugging
/*!
 Displays an ImGui panel for controlling camera behavior. Powered by CameraVm,
 it provides ergonomics for switching between orbit and fly modes, adjusting
 speeds, and viewing live debug information.

 ### Usage Examples

 ```cpp
 CameraControlPanel panel(vm);
 // Registered with DemoShell; the shell draws DrawContents() when active.
 ```

 @see CameraVm
 */
class CameraControlPanel final : public DemoPanel {
public:
  explicit CameraControlPanel(observer_ptr<CameraVm> vm);
  ~CameraControlPanel() override = default;

  //! Draws the panel content without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  void DrawCameraModeTab();
  void DrawDebugTab();
  void DrawCameraPoseInfo();
  void DrawInputDebugInfo();
  void DrawDroneMinimap();

  observer_ptr<CameraVm> vm_;
};

} // namespace oxygen::examples::ui
