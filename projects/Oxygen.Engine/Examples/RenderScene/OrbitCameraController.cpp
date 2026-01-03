//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "OrbitCameraController.h"
#include <algorithm>
#include <cmath>
#include <numbers>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace oxygen::examples::render_scene {

void OrbitCameraController::Update(
  scene::SceneNode& node, time::CanonicalDuration /*delta_time*/)
{
  if (std::abs(zoom_delta_) > 1e-6f) {
    ApplyZoom(zoom_delta_);
    zoom_delta_ = 0.0f;
  }

  if (glm::length(orbit_delta_) > 1e-6f) {
    ApplyOrbit(orbit_delta_);
    orbit_delta_ = { 0.0f, 0.0f };
  }

  if (glm::length(pan_delta_) > 1e-6f) {
    ApplyPan(pan_delta_);
    pan_delta_ = { 0.0f, 0.0f };
  }

  auto tf = node.GetTransform();
  glm::vec3 cam_pos;

  if (mode_ == OrbitMode::kTurntable) {
    const float cos_pitch = std::cos(turntable_pitch_);
    const float sin_pitch = std::sin(turntable_pitch_);
    const float cos_yaw = std::cos(turntable_yaw_);
    const float sin_yaw = std::sin(turntable_yaw_);

    const glm::vec3 dir_ws(sin_yaw * cos_pitch, cos_yaw * cos_pitch, sin_pitch);
    cam_pos = target_ + (dir_ws * distance_);

    const glm::vec3 world_up(0.0f, 0.0f, turntable_inverted_ ? -1.0f : 1.0f);
    const glm::vec3 forward_ws = glm::normalize(target_ - cam_pos);

    glm::vec3 right_ws = glm::cross(forward_ws, world_up);
    const float right_len2 = glm::dot(right_ws, right_ws);
    if (right_len2 <= 1e-8f) {
      const float sign = turntable_inverted_ ? 1.0f : -1.0f;
      right_ws
        = glm::normalize(glm::vec3(sign * cos_yaw, -sign * sin_yaw, 0.0f));
    } else {
      right_ws /= std::sqrt(right_len2);
    }
    const glm::vec3 up_ws = glm::cross(right_ws, forward_ws);

    glm::mat4 view_basis(1.0f);
    view_basis[0] = glm::vec4(right_ws, 0.0f);
    view_basis[1] = glm::vec4(up_ws, 0.0f);
    view_basis[2] = glm::vec4(-forward_ws, 0.0f);
    orbit_rot_ = glm::normalize(glm::quat_cast(view_basis));
  } else {
    cam_pos = target_ + (orbit_rot_ * glm::vec3(0.0f, 0.0f, distance_));
  }

  tf.SetLocalPosition(cam_pos);
  tf.SetLocalRotation(orbit_rot_);
}

void OrbitCameraController::SyncFromTransform(scene::SceneNode& node)
{
  auto tf = node.GetTransform();
  const auto pos = tf.GetLocalPosition().value_or(glm::vec3(0.0f));
  const auto rot
    = tf.GetLocalRotation().value_or(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

  const glm::vec3 forward = rot * glm::vec3(0.0f, 0.0f, -1.0f);
  distance_ = glm::distance(pos, target_);
  orbit_rot_ = rot;

  if (mode_ == OrbitMode::kTurntable) {
    turntable_yaw_ = std::atan2(forward.x, -forward.y);
    // Turntable pitch is defined by the camera position relative to the target
    // (pitch > 0 => camera above the target). Since `forward` points from the
    // camera to the target, its Z component has the opposite sign.
    turntable_pitch_ = -std::asin(std::clamp(forward.z, -1.0f, 1.0f));
  }
}

void OrbitCameraController::ApplyZoom(float delta)
{
  distance_
    = std::clamp(distance_ - delta * zoom_step_, min_distance_, max_distance_);
}

void OrbitCameraController::ApplyOrbit(const glm::vec2& delta)
{
  if (mode_ == OrbitMode::kTrackball) {
    const float phi0 = -delta.y * sensitivity_;
    const float phi1 = delta.x * sensitivity_;
    const glm::vec3 view_x_ws
      = glm::normalize(orbit_rot_ * glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 view_y_ws
      = glm::normalize(orbit_rot_ * glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 rot_vec_ws = (view_x_ws * phi0) + (view_y_ws * phi1);
    const float angle = glm::length(rot_vec_ws);
    if (angle > 1e-8f) {
      const glm::quat q_delta = glm::angleAxis(angle, rot_vec_ws / angle);
      orbit_rot_ = glm::normalize(q_delta * orbit_rot_);
    }
  } else {
    turntable_yaw_ += delta.x * sensitivity_;
    turntable_pitch_ += delta.y * sensitivity_;
    constexpr float kLimit = (std::numbers::pi_v<float> / 2.0f) - 0.01f;
    turntable_pitch_ = std::clamp(turntable_pitch_, -kLimit, kLimit);
  }
}

void OrbitCameraController::ApplyPan(const glm::vec2& delta)
{
  const glm::vec3 right = orbit_rot_ * glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 up = orbit_rot_ * glm::vec3(0.0f, 1.0f, 0.0f);
  target_ += (right * -delta.x + up * delta.y) * (distance_ * 0.001f);
}

} // namespace oxygen::examples::render_scene
