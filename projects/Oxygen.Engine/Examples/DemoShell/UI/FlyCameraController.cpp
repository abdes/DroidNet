//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

#include <glm/common.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Core/Time/Types.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "DemoShell/UI/FlyCameraController.h"
#include <glm/gtx/quaternion.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

namespace {
  // Near and far planes in OrthographicCamera::GetExtents().
  constexpr std::size_t kOrthoNearIndex = 4U;
  constexpr std::size_t kOrthoFarIndex = 5U;
} // namespace

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
  constexpr float kColinearEpsilon = 1e-8F;
  if (right_len2 <= kColinearEpsilon) {
    // Forward is nearly colinear with world up: pick an arbitrary right.
    right_ws = space::move::Right;
  } else {
    right_ws /= std::sqrt(right_len2);
  }
  const glm::vec3 up_ws = glm::normalize(glm::cross(right_ws, forward_ws));

  const glm::mat4 view_basis(glm::vec4(right_ws, 0.0F), glm::vec4(up_ws, 0.0F),
    glm::vec4(-glm::normalize(forward_ws), 0.0F),
    glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::quat orientation = glm::normalize(glm::quat_cast(view_basis));

  // 2. Handle Movement
  auto tf = node.GetTransform();
  glm::vec3 pos = tf.GetLocalPosition().value_or(glm::vec3(0.0F));

  if (glm::length(move_input_) > 0.0F) {
    glm::vec3 move_dir = glm::normalize(move_input_);

    float speed = move_speed_ * (boost_active_ ? boost_multiplier_ : 1.0F);

    // An orthographic image does not change with distance: forward/back
    // zooms its size, and panning scales with that size so it covers the same
    // share of the view as a perspective camera ten units away.
    if (auto ortho = node.GetCameraAs<scene::OrthographicCamera>(); ortho) {
      constexpr float kReferenceHeight = 10.0F;
      constexpr float kZoomRatePerSecond = 1.0F;
      constexpr float kMinHeight = 0.01F;
      auto ext = ortho->get().GetExtents();
      const float height = ext.at(3) - ext.at(2);
      if (std::abs(move_dir.z) > 0.0F) {
        const float boost = boost_active_ ? boost_multiplier_ : 1.0F;
        const float scale
          = std::max(std::exp(-move_dir.z * kZoomRatePerSecond * boost * dt),
            kMinHeight / height);
        const float centre_x = 0.5F * (ext.at(0) + ext.at(1));
        const float centre_y = 0.5F * (ext.at(2) + ext.at(3));
        ortho->get().SetExtents(centre_x + ((ext.at(0) - centre_x) * scale),
          centre_x + ((ext.at(1) - centre_x) * scale),
          centre_y + ((ext.at(2) - centre_y) * scale),
          centre_y + ((ext.at(3) - centre_y) * scale), ext.at(kOrthoNearIndex),
          ext.at(kOrthoFarIndex));
        move_dir.z = 0.0F;
      }
      speed *= height / kReferenceHeight;
    }

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
