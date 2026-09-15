//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Transforms/Decompose.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::examples::ui {
namespace {
  constexpr float kPanSensitivityScale = 0.001F;
  constexpr float kTurntablePitchLimitEpsilon = 0.01F;

  auto LiveWorldMatrix(scene::SceneNode node) -> std::optional<glm::mat4>
  {
    auto world = glm::mat4(1.0F);
    for (;;) {
      const auto impl = node.GetImpl();
      if (!impl) {
        return std::nullopt;
      }
      const auto& transform
        = impl->get().GetComponent<scene::detail::TransformComponent>();
      world = transform.GetLocalMatrix() * world;
      if (impl->get().GetFlags().GetEffectiveValue(
            scene::SceneNodeFlags::kIgnoreParentTransform)) {
        return world;
      }
      const auto parent = node.GetParent();
      if (!parent) {
        return world;
      }
      node = *parent;
    }
  }

  auto ParentWorldMatrix(scene::SceneNode& node) -> std::optional<glm::mat4>
  {
    const auto flags = node.GetFlags();
    if (!flags) {
      return std::nullopt;
    }
    if (flags->get().GetEffectiveValue(
          scene::SceneNodeFlags::kIgnoreParentTransform)) {
      return glm::mat4(1.0F);
    }
    const auto parent = node.GetParent();
    return parent ? LiveWorldMatrix(*parent)
                  : std::optional { glm::mat4(1.0F) };
  }

  auto SetWorldPose(scene::SceneNode& node, const glm::vec3& position,
    const glm::quat& rotation) -> bool
  {
    if (!node.HasParent()) {
      // SceneCameraViewResolver reads root position/rotation directly, without
      // decomposing the local scale into a different camera orientation.
      const auto scale = node.GetTransform().GetLocalScale();
      return scale
        && node.GetTransform().SetLocalTransform(position, rotation, *scale);
    }
    const auto parent = ParentWorldMatrix(node);
    const auto world = LiveWorldMatrix(node);
    const auto local_scale = node.GetTransform().GetLocalScale();
    if (!parent || !world || !local_scale) {
      return false;
    }
    glm::vec3 current_position;
    glm::quat current_rotation;
    glm::vec3 world_scale;
    if (!transforms::TryDecomposeTransform(
          *world, current_position, current_rotation, world_scale)) {
      return false;
    }

    // Use the same world rotation decomposition as the scene camera resolver.
    // Full matrices preserve nonuniform parent scaling. Supplying the original
    // local scale signs selects a compatible local rotation without changing
    // the authored scale (including mirrored camera transforms).
    const auto desired_world = glm::translate(glm::mat4(1.0F), position)
      * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0F), world_scale);
    const auto local = glm::inverse(*parent) * desired_world
      * glm::scale(glm::mat4(1.0F), glm::sign(*local_scale));
    glm::vec3 local_position;
    glm::quat local_rotation;
    glm::vec3 unused_scale;
    if (!transforms::TryDecomposeTransform(
          local, local_position, local_rotation, unused_scale)) {
      return false;
    }
    return node.GetTransform().SetLocalTransform(
      local_position, local_rotation, *local_scale);
  }
} // namespace

void OrbitCameraController::Update(
  scene::SceneNode& node, time::CanonicalDuration /*delta_time*/)
{
  if (!pose_update_pending_ && std::abs(zoom_delta_) <= math::Epsilon
    && glm::length(orbit_delta_) <= math::Epsilon
    && glm::length(pan_delta_) <= math::Epsilon) {
    return;
  }
  if (std::abs(zoom_delta_) > math::Epsilon) {
    ApplyZoom(zoom_delta_);
    zoom_delta_ = 0.0F;
  }

  const bool orbit_changed = glm::length(orbit_delta_) > math::Epsilon;
  if (orbit_changed) {
    ApplyOrbit(orbit_delta_);
    orbit_delta_ = { 0.0F, 0.0F };
  }

  if (glm::length(pan_delta_) > math::Epsilon) {
    ApplyPan(pan_delta_);
    pan_delta_ = { 0.0F, 0.0F };
  }

  // Adopting a camera does not remove its authored roll. Turntable rotation
  // constrains the camera to world-up only when the user actually orbits.
  if (mode_ == OrbitMode::kTurntable && orbit_changed) {
    const float cos_pitch = std::cos(turntable_pitch_);
    const float sin_pitch = std::sin(turntable_pitch_);
    const float cos_yaw = std::cos(turntable_yaw_);
    const float sin_yaw = std::sin(turntable_yaw_);

    const glm::vec3 forward_ws(
      sin_yaw * cos_pitch, -cos_yaw * cos_pitch, sin_pitch);

    const glm::vec3 world_up
      = space::move::Up * (turntable_inverted_ ? -1.0F : 1.0F);
    // forward_ws is already normalized and avoids the 0-distance singularity.
    const glm::vec3 forward_ws_norm = forward_ws;

    glm::vec3 right_ws = glm::cross(forward_ws_norm, world_up);
    const float right_len2 = glm::dot(right_ws, right_ws);
    if (right_len2 <= (math::Epsilon * math::Epsilon)) {
      const float sign = turntable_inverted_ ? 1.0F : -1.0F;
      right_ws
        = glm::normalize(glm::vec3(sign * cos_yaw, -sign * sin_yaw, 0.0F));
    } else {
      right_ws /= std::sqrt(right_len2);
    }
    const glm::vec3 up_ws = glm::cross(right_ws, forward_ws_norm);

    glm::mat4 view_basis(1.0F);
    view_basis[0] = glm::vec4(right_ws, 0.0F);
    view_basis[1] = glm::vec4(up_ws, 0.0F);
    view_basis[2] = glm::vec4(-forward_ws_norm, 0.0F);
    orbit_rot_ = glm::normalize(glm::quat_cast(view_basis));
  }

  const auto cam_pos
    = target_ - orbit_rot_ * (space::look::Forward * distance_);
  if (!SetWorldPose(node, cam_pos, orbit_rot_)) {
    LOG_F(ERROR,
      "Orbit camera '{}' cannot apply a world pose through its transform",
      node.GetName());
    return;
  }
  pose_update_pending_ = false;
}

void OrbitCameraController::SyncFromTransform(scene::SceneNode& node)
{
  // Input/binding can run before the scene's world-transform cache refresh.
  // Resolve the live local chain without forcing a scene-wide update.
  glm::vec3 pos;
  glm::quat rot;
  if (!node.HasParent()) {
    const auto local_position = node.GetTransform().GetLocalPosition();
    const auto local_rotation = node.GetTransform().GetLocalRotation();
    if (!local_position || !local_rotation) {
      return;
    }
    pos = *local_position;
    rot = *local_rotation;
  } else {
    const auto world = LiveWorldMatrix(node);
    glm::vec3 scale;
    if (!world || !transforms::TryDecomposeTransform(*world, pos, rot, scale)) {
      LOG_F(ERROR, "Orbit camera '{}' has no resolvable world pose",
        node.GetName());
      return;
    }
  }

  const glm::vec3 forward = rot * space::look::Forward;
  distance_ = std::max(min_distance_, glm::distance(pos, target_));
  // Keep the chosen radius, but place its pivot on the camera's view ray.
  // A stale/default target must not translate the camera on an idle update.
  target_ = pos + forward * distance_;
  orbit_rot_ = rot;
  pose_update_pending_ = false;

  if (mode_ == OrbitMode::kTurntable) {
    turntable_yaw_ = std::atan2(forward.x, -forward.y);
    // Turntable pitch follows engine conventions (Z-up, forward = -Y).
    turntable_pitch_ = std::asin(std::clamp(forward.z, -1.0F, 1.0F));
  }
}

void OrbitCameraController::ApplyZoom(float delta)
{
  distance_ = std::clamp(
    distance_ - (delta * zoom_step_), min_distance_, max_distance_);
}

void OrbitCameraController::ApplyOrbit(const glm::vec2& delta)
{
  if (mode_ == OrbitMode::kTrackball) {
    const float phi0 = -delta.y * sensitivity_;
    const float phi1 = delta.x * sensitivity_;
    const glm::vec3 view_x_ws = glm::normalize(orbit_rot_ * space::look::Right);
    const glm::vec3 view_y_ws = glm::normalize(orbit_rot_ * space::look::Up);
    const glm::vec3 rot_vec_ws = view_x_ws * phi0 + view_y_ws * phi1;
    const float angle = glm::length(rot_vec_ws);
    if (angle > math::Epsilon) {
      const glm::quat q_delta = glm::angleAxis(angle, rot_vec_ws / angle);
      orbit_rot_ = glm::normalize(q_delta * orbit_rot_);
    }
  } else {
    turntable_yaw_ += delta.x * sensitivity_;
    turntable_pitch_ += delta.y * sensitivity_;
    constexpr float kLimit
      = (std::numbers::pi_v<float> / 2.0F) - kTurntablePitchLimitEpsilon;
    turntable_pitch_ = std::clamp(turntable_pitch_, -kLimit, kLimit);
  }
}

void OrbitCameraController::ApplyPan(const glm::vec2& delta)
{
  const glm::vec3 right = orbit_rot_ * space::look::Right;
  const glm::vec3 up = orbit_rot_ * space::look::Up;
  target_
    += (right * -delta.x + up * delta.y) * (distance_ * kPanSensitivityScale);
}

} // namespace oxygen::examples::ui
