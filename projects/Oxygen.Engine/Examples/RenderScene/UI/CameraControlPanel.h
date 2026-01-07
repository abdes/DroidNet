//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::examples::render_scene {
class OrbitCameraController;
class FlyCameraController;
} // namespace oxygen::examples::render_scene

namespace oxygen::input {
class Action;
} // namespace oxygen::input

namespace oxygen::examples::render_scene::ui {

//! Camera control mode
enum class CameraControlMode { kOrbit, kFly };

//! Callback invoked when camera mode changes
using CameraModeChangeCallback = std::function<void(CameraControlMode)>;

//! Callback invoked when camera needs to be reset
using CameraResetCallback = std::function<void()>;

//! Configuration for camera control panel
struct CameraControlConfig {
  scene::SceneNode* active_camera { nullptr };
  OrbitCameraController* orbit_controller { nullptr };
  FlyCameraController* fly_controller { nullptr };

  // Input actions for debugging
  oxygen::input::Action* move_fwd_action { nullptr };
  oxygen::input::Action* move_bwd_action { nullptr };
  oxygen::input::Action* move_left_action { nullptr };
  oxygen::input::Action* move_right_action { nullptr };
  oxygen::input::Action* fly_boost_action { nullptr };
  oxygen::input::Action* fly_plane_lock_action { nullptr };
  oxygen::input::Action* rmb_action { nullptr };
  oxygen::input::Action* orbit_action { nullptr };

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

  //! Set current camera control mode
  void SetMode(CameraControlMode mode) { current_mode_ = mode; }

  //! Get current camera control mode
  [[nodiscard]] auto GetMode() const -> CameraControlMode
  {
    return current_mode_;
  }

private:
  void DrawCameraModeTab();
  void DrawDebugTab();
  void DrawCameraPoseInfo();
  void DrawInputDebugInfo();

  auto GetActionStateString(const oxygen::input::Action* action) const -> const
    char*;

  CameraControlConfig config_;
  CameraControlMode current_mode_ { CameraControlMode::kOrbit };
};

} // namespace oxygen::examples::render_scene::ui
