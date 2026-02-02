//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Time/Types.h>

namespace oxygen::scene {
class SceneNode;
}

namespace oxygen::examples::ui {

//! Autonomous camera controller following a spline path.
/*!
 A reusable camera controller for automated scene survey. Follows a closed
 Catmull-Rom spline with constant-speed arc-length traversal, POI slowdown
 zones, and cinematic dynamics (bob, noise, banking).

 ### Key Features
 - **Constant speed** via arc-length LUT (no acceleration at tight curves)
 - **POI slowdown** - reduces speed near points of interest
 - **Focus tracking** - looks toward a configurable target point
 - **Cinematics** - vertical bob, lateral noise, turn banking
 - **Path preview** - optional debug visualization of flight path

 @see CameraRigController
*/
class DroneCameraController {
public:
  using PathGenerator = std::function<std::vector<glm::vec3>()>;

  DroneCameraController();
  ~DroneCameraController();

  OXYGEN_MAKE_NON_COPYABLE(DroneCameraController)
  OXYGEN_MAKE_NON_MOVABLE(DroneCameraController)

  // --- Path Configuration ---

  //! Set path generator function (called once to build path and LUT).
  void SetPathGenerator(PathGenerator generator);

  //! Check if a valid path is configured.
  [[nodiscard]] auto HasPath() const noexcept -> bool;

  //! Get the generated path points (read-only).
  [[nodiscard]] auto GetPathPoints() const noexcept
    -> const std::vector<glm::vec3>&;

  // --- Speed & Dynamics ---

  //! Set base travel speed in world units per second.
  void SetSpeed(double units_per_sec);
  [[nodiscard]] auto GetSpeed() const noexcept -> double;

  //! Set damping factor for position/rotation smoothing (higher = stiffer).
  void SetDamping(double factor);
  [[nodiscard]] auto GetDamping() const noexcept -> double;

  //! Set ramp-up time for smooth motion start.
  void SetRampTime(double seconds);

  // --- Focus Target ---

  //! Set the focus target point the camera looks toward.
  void SetFocusTarget(glm::vec3 target);
  [[nodiscard]] auto GetFocusTarget() const noexcept -> glm::vec3;

  //! Set focus height (Y component of look-at target).
  void SetFocusHeight(float height);
  [[nodiscard]] auto GetFocusHeight() const noexcept -> float;

  // --- POI Slowdown ---

  //! Set points of interest where camera slows down.
  void SetPOIs(std::vector<glm::vec3> pois);

  //! Set slowdown activation radius around POIs.
  void SetPOISlowdownRadius(float radius);
  [[nodiscard]] auto GetPOISlowdownRadius() const noexcept -> float;

  //! Set minimum speed factor when near POI (0.3 = 30% of base speed).
  void SetPOIMinSpeedFactor(float factor);
  [[nodiscard]] auto GetPOIMinSpeedFactor() const noexcept -> float;

  // --- Cinematic Dynamics ---

  //! Set vertical bob amplitude.
  void SetBobAmplitude(double amp);
  [[nodiscard]] auto GetBobAmplitude() const noexcept -> double;

  //! Set vertical bob frequency in Hz.
  void SetBobFrequency(double hz);
  [[nodiscard]] auto GetBobFrequency() const noexcept -> double;

  //! Set lateral noise amplitude.
  void SetNoiseAmplitude(double amp);
  [[nodiscard]] auto GetNoiseAmplitude() const noexcept -> double;

  //! Set bank factor (roll into turns).
  void SetBankFactor(double factor);
  [[nodiscard]] auto GetBankFactor() const noexcept -> double;

  //! Set maximum bank angle in radians.
  void SetMaxBank(double radians);
  [[nodiscard]] auto GetMaxBank() const noexcept -> double;

  // --- Control ---

  //! Start flying along the path.
  void Start();

  //! Stop flying (pause at current position).
  void Stop();

  //! Check if currently flying.
  [[nodiscard]] auto IsFlying() const noexcept -> bool;

  //! Get progress along path (0.0 to 1.0).
  [[nodiscard]] auto GetProgress() const noexcept -> double;

  // --- Path Preview ---

  //! Enable/disable path preview visualization.
  void SetShowPathPreview(bool show);
  [[nodiscard]] auto GetShowPathPreview() const noexcept -> bool;

  // --- Update ---

  //! Sync controller state from camera's current transform.
  void SyncFromTransform(scene::SceneNode& camera);

  //! Update camera position/rotation (call each frame).
  void Update(scene::SceneNode& camera, time::CanonicalDuration delta_time);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::examples::ui
