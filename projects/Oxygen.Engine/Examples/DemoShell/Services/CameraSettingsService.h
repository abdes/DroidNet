//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::examples {

class SettingsService;

//! Settings persistence for camera control panel options.
/*!
 Owns UI-facing settings for camera control mode, orbit mode, and fly speed,
 delegating persistence to `SettingsService` and exposing an epoch for cache
 invalidation.

### Key Features

- **Passive state**: Reads and writes via SettingsService without caching.
- **Epoch tracking**: Increments on each effective change.
- **Testable**: Virtual getters and setters for overrides in tests.

@see SettingsService
*/
class CameraSettingsService {
public:
  using CameraControlMode = ui::CameraControlMode;
  using OrbitMode = ui::OrbitMode;

  CameraSettingsService() = default;
  virtual ~CameraSettingsService() = default;

  OXYGEN_MAKE_NON_COPYABLE(CameraSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(CameraSettingsService)

  //! Returns the persisted camera control mode.
  [[nodiscard]] virtual auto GetCameraControlMode() const -> CameraControlMode;

  //! Sets the camera control mode.
  virtual auto SetCameraControlMode(CameraControlMode mode) -> void;

  //! Sets the active camera id for per-camera rig settings.
  virtual auto SetActiveCameraId(std::string_view camera_id) -> void;

  //! Returns the persisted orbit mode.
  [[nodiscard]] virtual auto GetOrbitMode() const -> OrbitMode;

  //! Sets the orbit mode.
  virtual auto SetOrbitMode(OrbitMode mode) -> void;

  //! Returns the persisted fly move speed.
  [[nodiscard]] virtual auto GetFlyMoveSpeed() const -> float;

  //! Sets the fly move speed.
  virtual auto SetFlyMoveSpeed(float speed) -> void;

  // --- Drone Settings (per-camera rig) ---

  //! Returns the persisted drone speed.
  [[nodiscard]] virtual auto GetDroneSpeed() const -> float;

  //! Sets the drone speed.
  virtual auto SetDroneSpeed(float speed) -> void;

  //! Returns the persisted drone damping factor.
  [[nodiscard]] virtual auto GetDroneDamping() const -> float;

  //! Sets the drone damping factor.
  virtual auto SetDroneDamping(float damping) -> void;

  //! Returns the persisted drone focus height.
  [[nodiscard]] virtual auto GetDroneFocusHeight() const -> float;

  //! Sets the drone focus height.
  virtual auto SetDroneFocusHeight(float height) -> void;

  //! Returns the persisted drone focus offset X.
  [[nodiscard]] virtual auto GetDroneFocusOffsetX() const -> float;

  //! Sets the drone focus offset X.
  virtual auto SetDroneFocusOffsetX(float offset) -> void;

  //! Returns the persisted drone focus offset Y.
  [[nodiscard]] virtual auto GetDroneFocusOffsetY() const -> float;

  //! Sets the drone focus offset Y.
  virtual auto SetDroneFocusOffsetY(float offset) -> void;

  //! Returns whether the drone is currently running.
  [[nodiscard]] virtual auto GetDroneRunning() const -> bool;

  //! Sets the drone running state.
  virtual auto SetDroneRunning(bool running) -> void;

  //! Returns the persisted drone bob amplitude.
  [[nodiscard]] virtual auto GetDroneBobAmplitude() const -> float;

  //! Sets the drone bob amplitude.
  virtual auto SetDroneBobAmplitude(float amp) -> void;

  //! Returns the persisted drone bob frequency.
  [[nodiscard]] virtual auto GetDroneBobFrequency() const -> float;

  //! Sets the drone bob frequency.
  virtual auto SetDroneBobFrequency(float hz) -> void;

  //! Returns the persisted drone noise amplitude.
  [[nodiscard]] virtual auto GetDroneNoiseAmplitude() const -> float;

  //! Sets the drone noise amplitude.
  virtual auto SetDroneNoiseAmplitude(float amp) -> void;

  //! Returns the persisted drone bank factor.
  [[nodiscard]] virtual auto GetDroneBankFactor() const -> float;

  //! Sets the drone bank factor.
  virtual auto SetDroneBankFactor(float factor) -> void;

  //! Returns the persisted POI slowdown radius.
  [[nodiscard]] virtual auto GetDronePOISlowdownRadius() const -> float;

  //! Sets the POI slowdown radius.
  virtual auto SetDronePOISlowdownRadius(float radius) -> void;

  //! Returns the persisted POI minimum speed factor.
  [[nodiscard]] virtual auto GetDronePOIMinSpeed() const -> float;

  //! Sets the POI minimum speed factor.
  virtual auto SetDronePOIMinSpeed(float factor) -> void;

  //! Returns whether to show the flight path preview.
  [[nodiscard]] virtual auto GetDroneShowPath() const -> bool;

  //! Sets whether to show the flight path preview.
  virtual auto SetDroneShowPath(bool show) -> void;

  //! Returns the current settings epoch.
  [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

protected:
  //! Returns the settings service used for persistence.
  [[nodiscard]] virtual auto ResolveSettings() const noexcept
    -> observer_ptr<SettingsService>;

private:
  static constexpr auto kOrbitModeKey = "camera.orbit_mode";
  static constexpr auto kFlyMoveSpeedKey = "camera.fly_move_speed";

  static constexpr auto kDroneSpeedKey = "drone.speed";
  static constexpr auto kDroneDampingKey = "drone.damping";
  static constexpr auto kDroneFocusHeightKey = "drone.focus_height";
  static constexpr auto kDroneFocusOffsetXKey = "drone.focus_offset_x";
  static constexpr auto kDroneFocusOffsetYKey = "drone.focus_offset_y";
  static constexpr auto kDroneRunningKey = "drone.running";
  static constexpr auto kDroneBobAmpKey = "drone.bob_amp";
  static constexpr auto kDroneBobFreqKey = "drone.bob_freq";
  static constexpr auto kDroneNoiseAmpKey = "drone.noise_amp";
  static constexpr auto kDroneBankFactorKey = "drone.bank_factor";
  static constexpr auto kDronePOIRadiusKey = "drone.poi_radius";
  static constexpr auto kDronePOIMinSpeedKey = "drone.poi_min_speed";
  static constexpr auto kDroneShowPathKey = "drone.show_path";

  mutable std::atomic_uint64_t epoch_ { 0 };
  std::string active_camera_id_ {};
};

} // namespace oxygen::examples
