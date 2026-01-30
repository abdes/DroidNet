//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::input {
class Action;
}

namespace oxygen::examples {
class CameraLifecycleService;
class CameraSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class CameraRigController;

//! View model for camera control panel state.
/*!
 Bridges the UI-facing camera panel with the underlying camera simulation
 (CameraRigController) and lifecycle management (CameraLifecycleService).

### Key Features

- **Persistent Settings**: Syncs with CameraSettingsService for mode/speed.
- **Bi-directional Sync**: Pulls live camera poses from the simulation
  and pushes UI-driven changes back.
- **Input State Exposure**: Provides action states for debug visualization.
- **Thread-safe**: Protected by a mutex for multi-threaded access.
*/
class CameraVm {
public:
  using CameraControlMode = ui::CameraControlMode;
  using OrbitMode = ui::OrbitMode;

  explicit CameraVm(observer_ptr<CameraSettingsService> service,
    observer_ptr<CameraLifecycleService> camera_lifecycle,
    observer_ptr<CameraRigController> camera_rig);

  ~CameraVm() = default;

  // --- Perspectives & Modes ---

  [[nodiscard]] auto GetControlMode() -> CameraControlMode;
  auto SetControlMode(CameraControlMode mode) -> void;

  [[nodiscard]] auto GetOrbitMode() -> OrbitMode;
  auto SetOrbitMode(OrbitMode mode) -> void;

  [[nodiscard]] auto GetFlyMoveSpeed() -> float;
  auto SetFlyMoveSpeed(float speed) -> void;

  // --- Live Camera Data (Direct Pull) ---

  [[nodiscard]] auto HasActiveCamera() const -> bool;
  [[nodiscard]] auto GetCameraPosition() -> glm::vec3;
  [[nodiscard]] auto GetCameraRotation() -> glm::quat;

  // --- Input & Debug ---

  [[nodiscard]] auto GetActionStateString(const std::shared_ptr<input::Action>& action) const -> const char*;

  // Expose actions for the panel
  [[nodiscard]] auto GetMoveForwardAction() const -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveBackwardAction() const -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveLeftAction() const -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveRightAction() const -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetFlyBoostAction() const -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetFlyPlaneLockAction() const -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetRmbAction() const -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetOrbitAction() const -> std::shared_ptr<input::Action>;

  // --- Actions ---

  auto RequestReset() -> void;
  auto PersistActiveCameraSettings() -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  mutable std::mutex mutex_ {};
  observer_ptr<CameraSettingsService> service_;
  observer_ptr<CameraLifecycleService> camera_lifecycle_;
  observer_ptr<CameraRigController> camera_rig_;

  std::uint64_t epoch_ { 0 };

  // Cached settings state
  CameraControlMode control_mode_ { CameraControlMode::kOrbit };
  OrbitMode orbit_mode_ { OrbitMode::kTurntable };
  float fly_move_speed_ { 5.0f };
};

} // namespace oxygen::examples::ui
