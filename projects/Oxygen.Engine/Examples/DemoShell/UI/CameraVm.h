//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::input {
class Action;
}

namespace oxygen::examples {
class CameraSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class CameraRigController;

//! View model for camera control panel state.
/*!
 Bridges the UI-facing camera panel with the underlying camera simulation
 (CameraRigController) and settings/lifecycle management
(CameraSettingsService).

### Key Features

- **Persistent Settings**: Syncs with CameraSettingsService for mode/speed.
- **Bi-directional Sync**: Pulls live camera poses from the simulation
  and pushes UI-driven changes back.
- **Input State Exposure**: Provides action states for debug visualization.
- **Thread-safe**: Protected by a mutex for multi-threaded access.
*/
class CameraVm {
public:
  using CameraControlMode = CameraControlMode;
  using OrbitMode = OrbitMode;

  explicit CameraVm(observer_ptr<CameraSettingsService> service,
    observer_ptr<CameraRigController> camera_rig);

  ~CameraVm() = default;

  // --- Perspectives & Modes ---

  [[nodiscard]] auto GetControlMode() -> CameraControlMode;
  auto SetControlMode(CameraControlMode mode) -> void;

  [[nodiscard]] auto GetOrbitMode() -> OrbitMode;
  auto SetOrbitMode(OrbitMode mode) -> void;

  [[nodiscard]] auto GetFlyMoveSpeed() -> float;

  auto SetFlyMoveSpeed(float speed) -> void;

  // --- Drone Settings (Passthrough to Service + Rig) ---

  [[nodiscard]] auto IsDroneAvailable() const -> bool;
  [[nodiscard]] auto GetDroneProgress() const -> double;

  [[nodiscard]] auto GetDroneSpeed() -> float;
  auto SetDroneSpeed(float speed) -> void;

  [[nodiscard]] auto GetDroneDamping() -> float;
  auto SetDroneDamping(float damping) -> void;

  [[nodiscard]] auto GetDroneFocusHeight() -> float;
  auto SetDroneFocusHeight(float height) -> void;

  [[nodiscard]] auto GetDroneFocusOffset() -> glm::vec2;
  auto SetDroneFocusOffset(glm::vec2 offset) -> void;

  [[nodiscard]] auto GetDroneRunning() -> bool;
  auto SetDroneRunning(bool running) -> void;

  // Cinematics
  [[nodiscard]] auto GetDroneBobAmplitude() -> float;
  auto SetDroneBobAmplitude(float amp) -> void;

  [[nodiscard]] auto GetDroneBobFrequency() -> float;
  auto SetDroneBobFrequency(float hz) -> void;

  [[nodiscard]] auto GetDroneNoiseAmplitude() -> float;
  auto SetDroneNoiseAmplitude(float amp) -> void;

  [[nodiscard]] auto GetDroneBankFactor() -> float;
  auto SetDroneBankFactor(float factor) -> void;

  // POI
  [[nodiscard]] auto GetDronePOISlowdownRadius() -> float;
  auto SetDronePOISlowdownRadius(float radius) -> void;

  [[nodiscard]] auto GetDronePOIMinSpeed() -> float;
  auto SetDronePOIMinSpeed(float factor) -> void;

  // Debug
  [[nodiscard]] auto GetDroneShowPath() -> bool;
  auto SetDroneShowPath(bool show) -> void;

  // --- Live Camera Data (Direct Pull) ---

  [[nodiscard]] auto HasActiveCamera() const -> bool;
  [[nodiscard]] auto GetCameraPosition() -> glm::vec3;
  [[nodiscard]] auto GetCameraRotation() -> glm::quat;

  // --- Projection Settings (via active camera) ---

  [[nodiscard]] auto HasPerspectiveCamera() const -> bool;
  [[nodiscard]] auto HasOrthographicCamera() const -> bool;

  [[nodiscard]] auto GetPerspectiveFovDegrees() const -> float;
  auto SetPerspectiveFovDegrees(float fov_degrees) -> void;

  [[nodiscard]] auto GetPerspectiveNearPlane() const -> float;
  [[nodiscard]] auto GetPerspectiveFarPlane() const -> float;
  auto SetPerspectiveNearPlane(float near_plane) -> void;
  auto SetPerspectiveFarPlane(float far_plane) -> void;

  [[nodiscard]] auto GetOrthoWidth() const -> float;
  [[nodiscard]] auto GetOrthoHeight() const -> float;
  [[nodiscard]] auto GetOrthoNearPlane() const -> float;
  [[nodiscard]] auto GetOrthoFarPlane() const -> float;
  auto SetOrthoWidth(float width) -> void;
  auto SetOrthoHeight(float height) -> void;
  auto SetOrthoNearPlane(float near_plane) -> void;
  auto SetOrthoFarPlane(float far_plane) -> void;

  // --- Drone Path ---

  [[nodiscard]] auto GetDronePathPoints() const -> std::span<const glm::vec3>;

  // --- Input & Debug ---

  [[nodiscard]] auto GetActionStateString(
    const std::shared_ptr<input::Action>& action) const -> const char*;

  // Expose actions for the panel
  [[nodiscard]] auto GetMoveForwardAction() const
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveBackwardAction() const
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveLeftAction() const
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetMoveRightAction() const
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetFlyBoostAction() const
    -> std::shared_ptr<input::Action>;
  [[nodiscard]] auto GetFlyPlaneLockAction() const
    -> std::shared_ptr<input::Action>;
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
  observer_ptr<CameraRigController> camera_rig_;

  std::uint64_t epoch_ { 0 };

  // Cached settings state
  CameraControlMode control_mode_ { CameraControlMode::kOrbit };
  OrbitMode orbit_mode_ { OrbitMode::kTurntable };
  float fly_move_speed_ { 5.0F };
};

} // namespace oxygen::examples::ui
