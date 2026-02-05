//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/UI/FlyCameraController.h"

namespace oxygen::examples::ui {

void FlyCameraController::Update(
  scene::SceneNode& node, time::CanonicalDuration delta_time)
{
  const float dt = std::chrono::duration<float>(delta_time.get()).count();

  constexpr float kMaxPitchRad = glm::radians(89.0F);

  // 1. Handle Rotation (Look)
  yaw_ -= look_input_.x * look_sensitivity_;
  pitch_ -= look_input_.y * look_sensitivity_;

  // Constrain pitch to avoid flipping
  pitch_ = glm::clamp(pitch_, -kMaxPitchRad, kMaxPitchRad);

  // Z-up, world forward = -Y.
  // Yaw = 0 looks down -Y. Pitch > 0 looks upwards (+Z).
  const float cos_pitch = std::cos(pitch_);
  const float sin_pitch = std::sin(pitch_);
  const float cos_yaw = std::cos(yaw_);
  const float sin_yaw = std::sin(yaw_);

  const glm::vec3 forward_ws(
    sin_yaw * cos_pitch, -cos_yaw * cos_pitch, sin_pitch);
  constexpr glm::vec3 world_up = space::move::Up;

  glm::vec3 right_ws = glm::cross(forward_ws, world_up);
  const float right_len2 = glm::dot(right_ws, right_ws);
  if (right_len2 <= 1e-8f) {
    // Forward is nearly colinear with world up: pick an arbitrary right.
    right_ws = space::move::Right;
  } else {
    right_ws /= std::sqrt(right_len2);
  }
  const glm::vec3 up_ws = glm::normalize(glm::cross(right_ws, forward_ws));

  glm::mat4 view_basis(1.0F);
  view_basis[0] = glm::vec4(right_ws, 0.0F);
  view_basis[1] = glm::vec4(up_ws, 0.0F);
  view_basis[2] = glm::vec4(-glm::normalize(forward_ws), 0.0F);
  const glm::quat orientation = glm::normalize(glm::quat_cast(view_basis));

  // 2. Handle Movement
  auto tf = node.GetTransform();
  glm::vec3 pos = tf.GetLocalPosition().value_or(glm::vec3(0.0F));

  if (glm::length(move_input_) > 0.0F) {
    const glm::vec3 move_dir = glm::normalize(move_input_);

    const float speed
      = move_speed_ * (boost_active_ ? boost_multiplier_ : 1.0F);

    if (plane_lock_active_) {
      // Horizontal movement (no vertical gain): forward is -Y at yaw=0.
      const glm::vec3 forward(std::sin(yaw_), -std::cos(yaw_), 0.0F);
      const glm::vec3 right(std::cos(yaw_), std::sin(yaw_), 0.0F);

      pos += right * move_dir.x * speed * dt;
      pos += forward * move_dir.z * speed * dt;
      pos += world_up * move_dir.y * speed * dt;
    } else {
      // Movement is relative to full orientation (includes pitch).
      const glm::vec3 forward = orientation * space::look::Forward;
      const glm::vec3 right = orientation * space::look::Right;

      pos += right * move_dir.x * speed * dt;
      pos += forward * move_dir.z * speed * dt;
      // Vertical movement is world-up to keep controls intuitive.
      pos += world_up * move_dir.y * speed * dt;
    }
  }

  // 3. Apply to Node
  tf.SetLocalPosition(pos);
  tf.SetLocalRotation(orientation);

  // Reset inputs for next frame
  move_input_ = glm::vec3(0.0F);
  look_input_ = glm::vec2(0.0F);
}

void FlyCameraController::SyncFromTransform(scene::SceneNode& node)
{
  const auto tf = node.GetTransform();
  const glm::quat rot
    = tf.GetLocalRotation().value_or(glm::quat(1.0F, 0.0F, 0.0F, 0.0F));

  // Extract forward vector from rotation
  const glm::vec3 forward = rot * space::look::Forward;

  // Calculate yaw and pitch from forward vector (Z-up, forward=-Y reference).
  // forward_xy = (sin(yaw), -cos(yaw)) and forward.z = sin(pitch)
  pitch_ = std::asin(std::clamp(forward.z, -1.0F, 1.0F));
  yaw_ = std::atan2(forward.x, -forward.y);

  // Force an update to sanitize the rotation (remove roll) and ensure the
  // transform is consistent with the controller's state immediately.
  Update(node, time::CanonicalDuration(std::chrono::nanoseconds(0)));
}

} // namespace oxygen::examples::ui
