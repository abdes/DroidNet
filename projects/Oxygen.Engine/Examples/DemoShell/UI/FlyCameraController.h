//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

/**
 * @brief Standalone fly camera controller for the RenderScene example.
 * Provides WASD + Mouse look navigation.
 */
class FlyCameraController {
public:
  FlyCameraController() = default;

  /**
   * @brief Updates the node's transform based on current fly state.
   * @param node The SceneNode to manipulate.
   * @param delta_time Frame delta time.
   */
  void Update(scene::SceneNode& node, time::CanonicalDuration delta_time);

  // --- Input ---

  /**
   * @brief Adds movement input in local space.
   * @param input Vector where X=Right, Y=Up, Z=Forward.
   */
  void AddMovementInput(const glm::vec3& input) { move_input_ += input; }

  /**
   * @brief Adds rotation input (look around).
   * @param delta Vector where X=Yaw, Y=Pitch.
   */
  void AddRotationInput(const glm::vec2& delta) { look_input_ += delta; }

  // --- Configuration ---
  [[nodiscard]] auto GetMoveSpeed() const -> float { return move_speed_; }
  void SetMoveSpeed(float speed) { move_speed_ = speed; }
  //! Returns the current look sensitivity.
  [[nodiscard]] auto GetLookSensitivity() const noexcept -> float
  {
    return look_sensitivity_;
  }
  void SetLookSensitivity(float sensitivity)
  {
    look_sensitivity_ = sensitivity;
  }

  void SetBoostActive(bool active) { boost_active_ = active; }
  //! Returns whether boost is active.
  [[nodiscard]] auto GetBoostActive() const noexcept -> bool
  {
    return boost_active_;
  }
  void SetBoostMultiplier(float multiplier) { boost_multiplier_ = multiplier; }
  //! Returns the boost multiplier.
  [[nodiscard]] auto GetBoostMultiplier() const noexcept -> float
  {
    return boost_multiplier_;
  }
  void SetPlaneLockActive(bool active) { plane_lock_active_ = active; }
  //! Returns whether plane lock is active.
  [[nodiscard]] auto GetPlaneLockActive() const noexcept -> bool
  {
    return plane_lock_active_;
  }

  /**
   * @brief Synchronizes the controller state from the node's current transform.
   */
  void SyncFromTransform(scene::SceneNode& node);

private:
  float move_speed_ { 5.0f };
  // Mouse-look sensitivity in radians per input unit (typically pixels).
  float look_sensitivity_ { 0.0015f };

  bool boost_active_ { false };
  float boost_multiplier_ { 4.0f };
  bool plane_lock_active_ { false };

  glm::vec3 move_input_ { 0.0f };
  glm::vec2 look_input_ { 0.0f };

  // Stored in radians.
  float pitch_ { 0.0f };
  float yaw_ { 0.0f };
};

} // namespace oxygen::examples::render_scene
