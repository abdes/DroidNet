//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/Services/DomainService.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::examples {

class SettingsService;

namespace ui {
  class CameraRigController;
} // namespace ui

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
class CameraSettingsService : public DomainService {
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

  //! Bind the camera rig controller (optional).
  virtual auto BindCameraRig(observer_ptr<ui::CameraRigController> rig) -> void;

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
  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t override;

  //! Access the active camera handle.
  [[nodiscard]] auto GetActiveCamera() -> scene::SceneNode&
  {
    return active_camera_;
  }

  //! Access the active camera handle (const).
  [[nodiscard]] auto GetActiveCamera() const -> const scene::SceneNode&
  {
    return active_camera_;
  }

  //! Request a camera reset to initial pose.
  auto RequestReset() -> void;

  //! Persist the active camera state using SettingsService.
  auto PersistActiveCameraSettings() -> void;

  auto OnFrameStart(const engine::FrameContext& context) -> void override;
  auto OnSceneActivated(scene::Scene& scene) -> void override;
  auto OnMainViewReady(const engine::FrameContext& context,
    const renderer::CompositionView& view) -> void override;

private:
  // NOLINTBEGIN(*-magic-numbers)
  struct PersistedCameraState {
    struct TransformState {
      glm::vec3 position { 0.0F, 0.0F, 0.0F };
      glm::quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
      glm::vec3 scale { 1.0F, 1.0F, 1.0F };

      [[nodiscard]] auto IsDirty(const TransformState& other) const -> bool;
      void Persist(SettingsService& settings, const std::string& prefix) const;
    };

    struct PerspectiveState {
      bool enabled { false };
      float fov { 1.0F };
      float near_plane { 0.1F };
      float far_plane { 1000.0F };

      [[nodiscard]] auto IsDirty(const PerspectiveState& other) const -> bool;
      void Persist(SettingsService& settings, const std::string& prefix) const;
    };

    struct OrthoState {
      bool enabled { false };
      std::array<float, 6> extents {
        -1.0F,
        1.0F,
        -1.0F,
        1.0F,
        0.1F,
        1000.0F,
      };

      [[nodiscard]] auto IsDirty(const OrthoState& other) const -> bool;
      void Persist(SettingsService& settings, const std::string& prefix) const;
    };

    struct ExposureState {
      bool enabled { false };
      float aperture_f { 11.0F };
      float shutter_rate { 125.0F };
      float iso { 100.0F };

      [[nodiscard]] auto IsDirty(const ExposureState& other) const -> bool;
      void Persist(SettingsService& settings, const std::string& prefix) const;
    };

    bool valid { false };
    std::string camera_id;
    int camera_mode { 0 };
    TransformState transform {};
    PerspectiveState perspective {};
    OrthoState ortho {};
    ExposureState exposure {};

    glm::vec3 orbit_target { 0.0F, 0.0F, 0.0F };
    float orbit_distance { 5.0F };
    int orbit_mode { 0 };

    float fly_move_speed { 5.0F };
    float fly_look_sensitivity { 0.0015F };
    float fly_boost_multiplier { 4.0F };
    bool fly_plane_lock { false };

    [[nodiscard]] auto IsSameCamera(const PersistedCameraState& other) const
      -> bool;
  };

  void SetActiveCamera(scene::SceneNode camera);
  void SetActiveCameraId(std::string_view camera_id);
  void CaptureInitialPose();
  void EnsureFlyCameraFacingScene();
  void RequestSyncFromActive();
  void ApplyPendingSync();
  void ApplyPendingReset();
  void ApplyViewportToActive(float aspect, const ViewPort& viewport);
  [[nodiscard]] auto RestoreActiveCameraSettings() -> bool;
  [[nodiscard]] auto CaptureActiveCameraState() -> PersistedCameraState;

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

  observer_ptr<ui::CameraRigController> camera_rig_ { nullptr };
  scene::SceneNode active_camera_;
  glm::vec3 initial_camera_position_ { 0.0F, -15.0F, 0.0F };
  glm::quat initial_camera_rotation_ { 1.0F, 0.0F, 0.0F, 0.0F };
  bool pending_sync_ { false };
  bool pending_reset_ { false };
  PersistedCameraState last_saved_state_ {};
  mutable std::atomic_uint64_t epoch_ { 0 };
  std::string active_camera_id_;
  // NOLINTEND(*-magic-numbers)
};

} // namespace oxygen::examples
