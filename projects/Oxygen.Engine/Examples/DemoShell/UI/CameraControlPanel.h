//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "DemoShell/UI/FlyCameraController.h"
#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::input {
class Action;
} // namespace oxygen::input

namespace oxygen::examples::ui {

//! Camera control mode
enum class CameraControlMode { kOrbit, kFly };

//! Callback invoked when camera mode changes
using CameraModeChangeCallback = std::function<void(CameraControlMode)>;

//! Callback invoked when camera needs to be reset
using CameraResetCallback = std::function<void()>;

//! Configuration for camera control panel
/*!
 Provides non-owning access to camera state and controllers.

 ### Lifetime Guarantees

 - **Active Camera**: Points at the `MainModule::active_camera_` member, so
   camera switches update the handle in-place.
 - **Controllers**: Owned by `MainModule`. Call
   `MainModule::UpdateCameraControlPanelConfig()` after changing or recreating
   controllers to refresh the references.
 - **Actions**: Shared ownership to keep input actions alive while UI is
   rendering.

 @see CameraControlPanel::UpdateConfig
*/
struct CameraControlConfig {
  observer_ptr<scene::SceneNode> active_camera { nullptr };
  observer_ptr<OrbitCameraController> orbit_controller { nullptr };
  observer_ptr<FlyCameraController> fly_controller { nullptr };

  // Input actions for debugging
  std::shared_ptr<oxygen::input::Action> move_fwd_action {};
  std::shared_ptr<oxygen::input::Action> move_bwd_action {};
  std::shared_ptr<oxygen::input::Action> move_left_action {};
  std::shared_ptr<oxygen::input::Action> move_right_action {};
  std::shared_ptr<oxygen::input::Action> fly_boost_action {};
  std::shared_ptr<oxygen::input::Action> fly_plane_lock_action {};
  std::shared_ptr<oxygen::input::Action> rmb_action {};
  std::shared_ptr<oxygen::input::Action> orbit_action {};

  CameraModeChangeCallback on_mode_changed;
  CameraResetCallback on_reset_requested;
};

//! Camera control panel with mode switching and debugging
/*!
 Displays an ImGui panel for controlling camera behavior with separate tabs
 for camera mode selection and debug information. Provides ergonomic controls
 for switching between orbit and fly modes, adjusting camera parameters, and
 displaying real-time input state.

 ### Key Features

 - **Mode Switching:** Toggle between Orbit and Fly camera modes
 - **Orbit Controls:** Trackball vs Turntable selection
 - **Fly Controls:** Speed adjustment via mouse wheel
 - **Debug Tab:** Real-time input action states and camera pose
 - **Reset Function:** Restore camera to initial position

 ### Usage Examples

 ```cpp
 CameraControlPanel panel;
 CameraControlConfig config;
 config.active_camera = &active_camera_;
 config.orbit_controller = orbit_controller_.get();
 config.fly_controller = fly_controller_.get();
 config.on_mode_changed = [this](CameraControlMode mode) {
   camera_mode_ = mode;
   UpdateActiveCameraInputContext();
 };
 config.on_reset_requested = [this]() {
   ResetCameraToInitialPose();
 };

 panel.Initialize(config);

 // In ImGui update loop
 panel.Draw();
 ```

 @see CameraControlConfig, CameraControlMode
 */
class CameraControlPanel {
public:
  CameraControlPanel() = default;
  ~CameraControlPanel() = default;

  //! Initialize panel with configuration
  void Initialize(const CameraControlConfig& config);

  //! Update panel configuration (e.g., when camera changes)
  void UpdateConfig(const CameraControlConfig& config);

  //! Draw the ImGui panel content
  /*!
   Renders the camera control UI with tabs for mode control and debugging.

   @note Must be called within ImGui rendering context
   */
  void Draw();

  //! Draws the panel content without creating a window.
  void DrawContents();

  //! Set current camera control mode
  void SetMode(CameraControlMode mode) { current_mode_ = mode; }

  //! Get current camera control mode
  [[nodiscard]] auto GetMode() const -> CameraControlMode
  {
    return current_mode_;
  }

private:
  auto LoadSettings() -> void;
  auto SaveModeSetting() const -> void;
  auto SaveOrbitModeSetting(OrbitMode mode) const -> void;
  auto SaveFlySpeedSetting(float speed) const -> void;

  void DrawCameraModeTab();
  void DrawDebugTab();
  void DrawCameraPoseInfo();
  void DrawInputDebugInfo();

  auto GetActionStateString(
    const std::shared_ptr<oxygen::input::Action>& action) const -> const char*;

  CameraControlConfig config_;
  CameraControlMode current_mode_ { CameraControlMode::kOrbit };
  bool settings_loaded_ { false };
};

} // namespace oxygen::examples::ui
