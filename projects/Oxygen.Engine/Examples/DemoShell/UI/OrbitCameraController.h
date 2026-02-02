//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

enum class OrbitMode { kTurntable, kTrackball };

/**
 * @brief Standalone orbit camera controller for the RenderScene example.
 * Manages camera transformation logic without being an engine component.
 */
class OrbitCameraController {
public:
  OrbitCameraController() = default;

  /**
   * @brief Updates the node's transform based on current orbit state.
   * @param node The SceneNode to manipulate (must have a transform).
   * @param delta_time Frame delta time.
   */
  void Update(scene::SceneNode& node, time::CanonicalDuration delta_time);

  // --- Input ---
  void AddOrbitInput(const glm::vec2& delta) { orbit_delta_ += delta; }
  void AddZoomInput(float delta) { zoom_delta_ += delta; }
  void AddPanInput(const glm::vec2& delta) { pan_delta_ += delta; }

  // --- Configuration ---
  void SetTarget(const glm::vec3& target) { target_ = target; }
  //! Returns the current orbit target in world space.
  [[nodiscard]] auto GetTarget() const noexcept -> const glm::vec3&
  {
    return target_;
  }
  void SetDistance(float distance) { distance_ = distance; }
  //! Returns the current orbit distance.
  [[nodiscard]] auto GetDistance() const noexcept -> float { return distance_; }
  void SetMode(OrbitMode mode) { mode_ = mode; }
  [[nodiscard]] auto GetMode() const noexcept -> OrbitMode { return mode_; }
  //! Returns the orbit sensitivity.
  [[nodiscard]] auto GetSensitivity() const noexcept -> float
  {
    return sensitivity_;
  }
  //! Returns the zoom step size.
  [[nodiscard]] auto GetZoomStep() const noexcept -> float
  {
    return zoom_step_;
  }
  //! Returns the minimum orbit distance.
  [[nodiscard]] auto GetMinDistance() const noexcept -> float
  {
    return min_distance_;
  }
  //! Returns the maximum orbit distance.
  [[nodiscard]] auto GetMaxDistance() const noexcept -> float
  {
    return max_distance_;
  }

  /**
   * @brief Synchronizes the controller state from the node's current transform.
   */
  void SyncFromTransform(scene::SceneNode& node);

private:
  void ApplyZoom(float delta);
  void ApplyOrbit(const glm::vec2& delta);
  void ApplyPan(const glm::vec2& delta);

  OrbitMode mode_ { OrbitMode::kTurntable };

  glm::vec3 target_ { 0.0F };
  float distance_ { 5.0F };
  glm::quat orbit_rot_ { 1.0F, 0.0F, 0.0F, 0.0F };

  // Turntable state
  float turntable_yaw_ { 0.0F };
  float turntable_pitch_ { 0.0F };
  bool turntable_inverted_ { false };

  // Input accumulation
  glm::vec2 orbit_delta_ { 0.0F };
  float zoom_delta_ { 0.0F };
  glm::vec2 pan_delta_ { 0.0F };

  // Settings
  float sensitivity_ { 0.005F };
  float zoom_step_ { 0.5F };
  float min_distance_ { 0.1F };
  float max_distance_ { 100.0F };
};

} // namespace oxygen::examples::ui
