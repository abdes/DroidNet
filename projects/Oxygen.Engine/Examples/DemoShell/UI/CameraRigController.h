//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Time/Types.h>

#include "DemoShell/UI/CameraControlPanel.h"

namespace oxygen::engine {
class InputSystem;
} // namespace oxygen::engine

namespace oxygen::input {
class Action;
class InputMappingContext;
} // namespace oxygen::input

namespace oxygen::scene {
class SceneNode;
} // namespace oxygen::scene

namespace oxygen::examples::ui {

class OrbitCameraController;
class FlyCameraController;
class DroneCameraController;

//! Orchestrates camera input, controllers, and mode switching.
/*!
 Centralizes camera control wiring for demo shells by managing input bindings,
 controller state, and mode switching logic. The controller owns orbit and fly
 camera controllers and applies input each frame to the active camera node.

 ### Key Features

 - **Mode Switching**: Activates orbit or fly input contexts on demand.
 - **Input Ownership**: Creates and stores camera-related input actions.
 - **Controller Sync**: Keeps controllers synchronized with active camera.

 @see CameraControlPanel, OrbitCameraController, FlyCameraController
*/
class CameraRigController {
public:
  CameraRigController();
  ~CameraRigController();

  OXYGEN_MAKE_NON_COPYABLE(CameraRigController);
  OXYGEN_MAKE_NON_MOVABLE(CameraRigController);

  //! Initialize the input bindings required for camera controls.
  auto Initialize(observer_ptr<engine::InputSystem> input_system) -> bool;

  //! Assign the active camera node for control and syncing.
  auto SetActiveCamera(observer_ptr<scene::SceneNode> camera) -> void;

  //! Retrieve the current active camera.
  [[nodiscard]] auto GetActiveCamera() const noexcept
    -> observer_ptr<scene::SceneNode>;

  //! Switch between orbit and fly camera control modes.
  auto SetMode(CameraControlMode mode) -> void;

  //! Retrieve the current camera control mode.
  [[nodiscard]] auto GetMode() const noexcept -> CameraControlMode;

  //! Apply accumulated input to the active camera for the frame.
  auto Update(time::CanonicalDuration delta_time) -> void;

  //! Sync controllers from the active camera's transform.
  auto SyncFromActiveCamera() -> void;

  //! Access the orbit controller instance.
  [[nodiscard]] auto GetOrbitController() noexcept
    -> observer_ptr<OrbitCameraController>;

  //! Access the fly controller instance.
  [[nodiscard]] auto GetFlyController() noexcept
    -> observer_ptr<FlyCameraController>;

  //! Access the drone controller instance.
  [[nodiscard]] auto GetDroneController() noexcept
    -> observer_ptr<DroneCameraController>;

  //! Check if drone mode is available (has a valid path).
  [[nodiscard]] auto IsDroneAvailable() const noexcept -> bool;

  //! Access shared input actions for UI debugging.
  [[nodiscard]] auto GetZoomInAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetZoomOutAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetRmbAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetOrbitAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveForwardAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveBackwardAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveLeftAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveRightAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveUpAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveDownAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetFlyPlaneLockAction() const noexcept
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetFlyBoostAction() const noexcept
    -> std::shared_ptr<input::Action>;

private:
  auto UpdateActiveCameraInputContext() -> void;
  auto EnsureControllers() -> void;

  observer_ptr<engine::InputSystem> input_system_ { nullptr };
  observer_ptr<scene::SceneNode> active_camera_ { nullptr };

  std::shared_ptr<input::Action> zoom_in_action_ {};
  std::shared_ptr<input::Action> zoom_out_action_ {};
  std::shared_ptr<input::Action> rmb_action_ {};
  std::shared_ptr<input::Action> orbit_action_ {};
  std::shared_ptr<input::Action> move_fwd_action_ {};
  std::shared_ptr<input::Action> move_bwd_action_ {};
  std::shared_ptr<input::Action> move_left_action_ {};
  std::shared_ptr<input::Action> move_right_action_ {};
  std::shared_ptr<input::Action> move_up_action_ {};
  std::shared_ptr<input::Action> move_down_action_ {};
  std::shared_ptr<input::Action> fly_plane_lock_action_ {};
  std::shared_ptr<input::Action> fly_boost_action_ {};

  std::shared_ptr<input::InputMappingContext> orbit_controls_ctx_ {};
  std::shared_ptr<input::InputMappingContext> fly_controls_ctx_ {};

  CameraControlMode current_mode_ { CameraControlMode::kFly };
  std::unique_ptr<OrbitCameraController> orbit_controller_ {};
  std::unique_ptr<FlyCameraController> fly_controller_ {};
  std::unique_ptr<DroneCameraController> drone_controller_ {};
};

} // namespace oxygen::examples::ui
