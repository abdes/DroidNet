//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/UI/CameraRigController.h"

namespace oxygen::examples {

namespace {

  auto MakeLookRotationFromPosition(const glm::vec3& position,
    const glm::vec3& target,
    const glm::vec3& up_direction = { 0.0F, 0.0F, 1.0F }) -> glm::quat
  {
    const auto forward_raw = target - position;
    const float forward_len2 = glm::dot(forward_raw, forward_raw);
    if (forward_len2 <= 1e-8F) {
      return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    }

    const auto forward = glm::normalize(forward_raw);
    // Avoid singularities when forward is colinear with up.
    glm::vec3 up_dir = up_direction;
    const float dot_abs = std::abs(glm::dot(forward, glm::normalize(up_dir)));
    if (dot_abs > 0.999F) {
      // Pick an alternate up that is guaranteed to be non-colinear.
      up_dir = (std::abs(forward.z) > 0.9F) ? glm::vec3(0.0F, 1.0F, 0.0F)
                                            : glm::vec3(0.0F, 0.0F, 1.0F);
    }

    const auto right_raw = glm::cross(forward, up_dir);
    const float right_len2 = glm::dot(right_raw, right_raw);
    if (right_len2 <= 1e-8F) {
      return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    }

    const auto right = right_raw / std::sqrt(right_len2);
    const auto up = glm::cross(right, forward);

    glm::mat4 look_matrix(1.0F);
    // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
    look_matrix[0] = glm::vec4(right, 0.0F);
    look_matrix[1] = glm::vec4(up, 0.0F);
    look_matrix[2] = glm::vec4(-forward, 0.0F);
    // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)

    return glm::quat_cast(look_matrix);
  }

} // namespace

void CameraLifecycleService::SetScene(std::shared_ptr<scene::Scene> scene)
{
  if (scene_ == scene) {
    return;
  }

  scene_ = std::move(scene);
  active_camera_ = {};
}

void CameraLifecycleService::BindCameraRig(
  observer_ptr<ui::CameraRigController> rig)
{
  camera_rig_ = rig;
  if (camera_rig_) {
    if (active_camera_.IsAlive()) {
      camera_rig_->SetActiveCamera(observer_ptr { &active_camera_ });
    } else {
      camera_rig_->SetActiveCamera(nullptr);
    }
  }
}

void CameraLifecycleService::SetActiveCamera(scene::SceneNode camera)
{
  active_camera_ = std::move(camera);
  if (camera_rig_) {
    camera_rig_->SetActiveCamera(observer_ptr { &active_camera_ });
  }
}

void CameraLifecycleService::CaptureInitialPose()
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  auto tf = active_camera_.GetTransform();
  if (auto pos = tf.GetLocalPosition()) {
    initial_camera_position_ = *pos;
  }
  if (auto rot = tf.GetLocalRotation()) {
    initial_camera_rotation_ = *rot;
  }
}

void CameraLifecycleService::EnsureViewport(const int width, const int height)
{
  if (!active_camera_.IsAlive()) {
    EnsureFallbackCamera();
  }

  if (!active_camera_.IsAlive()) {
    return;
  }

  const float aspect = height > 0
    ? (static_cast<float>(width) / static_cast<float>(height))
    : 1.0F;

  const ViewPort viewport { .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F };

  ApplyViewportToActive(aspect, viewport);
}

void CameraLifecycleService::EnsureFlyCameraFacingScene()
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  if (!camera_rig_ || camera_rig_->GetMode() != ui::CameraControlMode::kFly) {
    return;
  }

  auto tf = active_camera_.GetTransform();
  const glm::vec3 cam_pos = tf.GetLocalPosition().value_or(glm::vec3(0.0F));
  const glm::quat cam_rot
    = tf.GetLocalRotation().value_or(glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
  const glm::vec3 forward = cam_rot * oxygen::space::look::Forward;
  const glm::vec3 target(0.0F, 0.0F, 0.0F);
  const glm::vec3 to_target = target - cam_pos;
  const float len2 = glm::dot(to_target, to_target);
  if (len2 <= 1e-6F) {
    return;
  }

  const glm::vec3 to_target_dir = to_target / std::sqrt(len2);
  if (glm::dot(forward, to_target_dir) >= 0.0F) {
    return;
  }

  const glm::quat look_rot = MakeLookRotationFromPosition(cam_pos, target);
  tf.SetLocalRotation(look_rot);
  initial_camera_rotation_ = look_rot;

  if (camera_rig_) {
    camera_rig_->SyncFromActiveCamera();
  }
}

void CameraLifecycleService::RequestSyncFromActive() { pending_sync_ = true; }

void CameraLifecycleService::ApplyPendingSync()
{
  if (!pending_sync_ || !active_camera_.IsAlive()) {
    return;
  }

  if (camera_rig_) {
    camera_rig_->SyncFromActiveCamera();
  }

  pending_sync_ = false;
}

void CameraLifecycleService::RequestReset() { pending_reset_ = true; }

void CameraLifecycleService::ApplyPendingReset()
{
  if (!pending_reset_ || !active_camera_.IsAlive()) {
    return;
  }

  auto transform = active_camera_.GetTransform();
  transform.SetLocalPosition(initial_camera_position_);
  transform.SetLocalRotation(initial_camera_rotation_);

  if (camera_rig_) {
    camera_rig_->SyncFromActiveCamera();
  }

  pending_reset_ = false;
  LOG_F(INFO, "Camera reset to initial pose");
}

void CameraLifecycleService::Clear()
{
  active_camera_ = {};
  pending_sync_ = false;
  pending_reset_ = false;
  if (camera_rig_) {
    camera_rig_->SetActiveCamera(nullptr);
  }
}

void CameraLifecycleService::EnsureFallbackCamera()
{
  using scene::PerspectiveCamera;

  if (!scene_) {
    return;
  }

  if (!active_camera_.IsAlive()) {
    active_camera_ = scene_->CreateNode("MainCamera");

    // Camera at -Y axis looking at origin with Z-up.
    // User is at (0, -15, 0) watching the scene at origin.
    const glm::vec3 cam_pos(0.0F, -15.0F, 0.0F);
    const glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
    const glm::quat cam_rot = MakeLookRotationFromPosition(cam_pos, cam_target);

    auto tf = active_camera_.GetTransform();
    tf.SetLocalPosition(cam_pos);
    tf.SetLocalRotation(cam_rot);

    initial_camera_position_ = cam_pos;
    initial_camera_target_ = cam_target;
    initial_camera_rotation_ = cam_rot;

    if (camera_rig_) {
      camera_rig_->SetActiveCamera(observer_ptr { &active_camera_ });
    }
  }

  if (!active_camera_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
    const bool attached = active_camera_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  }
}

void CameraLifecycleService::ApplyViewportToActive(
  const float aspect, const ViewPort& viewport)
{
  if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
    cam_ref) {
    auto& cam = cam_ref->get();
    cam.SetAspectRatio(aspect);
    cam.SetViewport(viewport);
    return;
  }

  if (auto ortho_ref = active_camera_.GetCameraAs<scene::OrthographicCamera>();
    ortho_ref) {
    ortho_ref->get().SetViewport(viewport);
    return;
  }

  EnsureFallbackCamera();
  if (active_camera_.IsAlive()) {
    if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
      cam_ref) {
      auto& cam = cam_ref->get();
      cam.SetAspectRatio(aspect);
      cam.SetViewport(viewport);
    }
  }
}

} // namespace oxygen::examples
