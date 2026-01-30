//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>

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

  //! Returns the persisted orbit mode.
  [[nodiscard]] virtual auto GetOrbitMode() const -> OrbitMode;

  //! Sets the orbit mode.
  virtual auto SetOrbitMode(OrbitMode mode) -> void;

  //! Returns the persisted fly move speed.
  [[nodiscard]] virtual auto GetFlyMoveSpeed() const -> float;

  //! Sets the fly move speed.
  virtual auto SetFlyMoveSpeed(float speed) -> void;

  //! Returns the current settings epoch.
  [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

protected:
  //! Returns the settings service used for persistence.
  [[nodiscard]] virtual auto ResolveSettings() const noexcept
    -> observer_ptr<SettingsService>;

private:
  static constexpr const char* kControlModeKey = "camera.control_mode";
  static constexpr const char* kOrbitModeKey = "camera.orbit_mode";
  static constexpr const char* kFlyMoveSpeedKey = "camera.fly_move_speed";

  mutable std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples
