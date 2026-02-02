//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/FlyCameraController.h"
#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::examples {

namespace {

  constexpr float kPersistEpsilon = 1e-4F;

  auto NearlyEqual(const float lhs, const float rhs) -> bool
  {
    return std::abs(lhs - rhs) <= kPersistEpsilon;
  }

  auto NearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs) -> bool
  {
    return NearlyEqual(lhs.x, rhs.x) && NearlyEqual(lhs.y, rhs.y)
      && NearlyEqual(lhs.z, rhs.z);
  }

  auto NearlyEqual(const glm::quat& lhs, const glm::quat& rhs) -> bool
  {
    return NearlyEqual(lhs.x, rhs.x) && NearlyEqual(lhs.y, rhs.y)
      && NearlyEqual(lhs.z, rhs.z) && NearlyEqual(lhs.w, rhs.w);
  }

  auto CameraModeToString(const ui::CameraControlMode mode) -> std::string_view
  {
    if (mode == ui::CameraControlMode::kOrbit) {
      return "orbit";
    }
    if (mode == ui::CameraControlMode::kDrone) {
      return "drone";
    }
    return "fly";
  }

  auto ParseCameraMode(const std::string_view value)
    -> std::optional<ui::CameraControlMode>
  {
    if (value == "orbit") {
      return ui::CameraControlMode::kOrbit;
    }
    if (value == "drone") {
      return ui::CameraControlMode::kDrone;
    }
    if (value == "fly") {
      return ui::CameraControlMode::kFly;
    }
    return std::nullopt;
  }

  auto OrbitModeToString(const ui::OrbitMode mode) -> std::string_view
  {
    return mode == ui::OrbitMode::kTrackball ? "trackball" : "turntable";
  }

  auto ParseOrbitMode(const std::string_view value)
    -> std::optional<ui::OrbitMode>
  {
    if (value == "trackball") {
      return ui::OrbitMode::kTrackball;
    }
    if (value == "turntable") {
      return ui::OrbitMode::kTurntable;
    }
    return std::nullopt;
  }

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

void CameraLifecycleService::SetScene(observer_ptr<scene::Scene> scene)
{
  if (scene_ == scene) {
    return;
  }

  scene_ = scene;
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
  RestoreActiveCameraSettings();
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
  const glm::vec3 forward = cam_rot * space::look::Forward;
  constexpr glm::vec3 target(0.0F, 0.0F, 0.0F);
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

  camera_rig_->SyncFromActiveCamera();
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

void CameraLifecycleService::PersistActiveCameraSettings()
{
  const auto settings = SettingsService::Default();
  if (!settings) {
    return;
  }
  const auto current = CaptureActiveCameraState();
  if (!current.valid) {
    return;
  }

  const bool same_camera = last_saved_state_.valid
    && last_saved_state_.camera_id == current.camera_id;
  const bool unchanged = same_camera
    && last_saved_state_.camera_mode == current.camera_mode
    && NearlyEqual(last_saved_state_.position, current.position)
    && NearlyEqual(last_saved_state_.rotation, current.rotation)
    && NearlyEqual(last_saved_state_.scale, current.scale)
    && last_saved_state_.has_perspective == current.has_perspective
    && (!current.has_perspective
      || (NearlyEqual(
            last_saved_state_.perspective_fov, current.perspective_fov)
        && NearlyEqual(
          last_saved_state_.perspective_near, current.perspective_near)
        && NearlyEqual(
          last_saved_state_.perspective_far, current.perspective_far)))
    && last_saved_state_.has_orthographic == current.has_orthographic
    && (!current.has_orthographic
      || (std::equal(current.ortho_extents.begin(), current.ortho_extents.end(),
        last_saved_state_.ortho_extents.begin(),
        [](
          const float lhs, const float rhs) { return NearlyEqual(lhs, rhs); })))
    && NearlyEqual(last_saved_state_.orbit_target, current.orbit_target)
    && NearlyEqual(last_saved_state_.orbit_distance, current.orbit_distance)
    && last_saved_state_.orbit_mode == current.orbit_mode
    && NearlyEqual(last_saved_state_.fly_move_speed, current.fly_move_speed)
    && NearlyEqual(
      last_saved_state_.fly_look_sensitivity, current.fly_look_sensitivity)
    && NearlyEqual(
      last_saved_state_.fly_boost_multiplier, current.fly_boost_multiplier)
    && last_saved_state_.fly_plane_lock == current.fly_plane_lock;

  if (unchanged) {
    return;
  }

  const std::string prefix = "camera_rig." + current.camera_id;

  settings->SetString(prefix + ".mode",
    std::string(CameraModeToString(
      static_cast<ui::CameraControlMode>(current.camera_mode))));

  settings->SetFloat(prefix + ".position.x", current.position.x);
  settings->SetFloat(prefix + ".position.y", current.position.y);
  settings->SetFloat(prefix + ".position.z", current.position.z);

  settings->SetFloat(prefix + ".rotation.x", current.rotation.x);
  settings->SetFloat(prefix + ".rotation.y", current.rotation.y);
  settings->SetFloat(prefix + ".rotation.z", current.rotation.z);
  settings->SetFloat(prefix + ".rotation.w", current.rotation.w);

  settings->SetFloat(prefix + ".scale.x", current.scale.x);
  settings->SetFloat(prefix + ".scale.y", current.scale.y);
  settings->SetFloat(prefix + ".scale.z", current.scale.z);

  settings->SetBool(
    prefix + ".camera.has_perspective", current.has_perspective);
  if (current.has_perspective) {
    settings->SetFloat(
      prefix + ".camera.perspective.fov", current.perspective_fov);
    settings->SetFloat(
      prefix + ".camera.perspective.near", current.perspective_near);
    settings->SetFloat(
      prefix + ".camera.perspective.far", current.perspective_far);
  }

  settings->SetBool(
    prefix + ".camera.has_orthographic", current.has_orthographic);
  if (current.has_orthographic) {
    settings->SetFloat(prefix + ".camera.ortho.left", current.ortho_extents[0]);
    settings->SetFloat(
      prefix + ".camera.ortho.right", current.ortho_extents[1]);
    settings->SetFloat(
      prefix + ".camera.ortho.bottom", current.ortho_extents[2]);
    settings->SetFloat(prefix + ".camera.ortho.top", current.ortho_extents[3]);
    settings->SetFloat(prefix + ".camera.ortho.near", current.ortho_extents[4]);
    settings->SetFloat(prefix + ".camera.ortho.far", current.ortho_extents[5]);
  }

  settings->SetFloat(prefix + ".orbit.target.x", current.orbit_target.x);
  settings->SetFloat(prefix + ".orbit.target.y", current.orbit_target.y);
  settings->SetFloat(prefix + ".orbit.target.z", current.orbit_target.z);
  settings->SetFloat(prefix + ".orbit.distance", current.orbit_distance);
  settings->SetString(prefix + ".orbit.mode",
    std::string(
      OrbitModeToString(static_cast<ui::OrbitMode>(current.orbit_mode))));

  settings->SetFloat(prefix + ".fly.move_speed", current.fly_move_speed);
  settings->SetFloat(
    prefix + ".fly.look_sensitivity", current.fly_look_sensitivity);
  settings->SetFloat(
    prefix + ".fly.boost_multiplier", current.fly_boost_multiplier);
  settings->SetBool(prefix + ".fly.plane_lock", current.fly_plane_lock);

  last_saved_state_ = current;
}

void CameraLifecycleService::Clear()
{
  active_camera_ = {};
  pending_sync_ = false;
  pending_reset_ = false;
  if (camera_rig_) {
    camera_rig_->SetActiveCamera(nullptr);
  }
  last_saved_state_ = {};
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
    constexpr glm::vec3 cam_pos(0.0F, -15.0F, 0.0F);
    constexpr glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
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

  RestoreActiveCameraSettings();
}

void CameraLifecycleService::RestoreActiveCameraSettings()
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  const auto settings = SettingsService::Default();
  if (!settings) {
    return;
  }

  const std::string camera_id = active_camera_.GetName();
  if (camera_id.empty()) {
    return;
  }

  const std::string prefix = "camera_rig." + camera_id;

  if (camera_rig_) {
    if (const auto mode_str = settings->GetString(prefix + ".mode")) {
      if (const auto mode = ParseCameraMode(*mode_str)) {
        camera_rig_->SetMode(*mode);
      }
    }
  }

  auto tf = active_camera_.GetTransform();
  glm::vec3 pos = tf.GetLocalPosition().value_or(glm::vec3(0.0F));
  glm::quat rot
    = tf.GetLocalRotation().value_or(glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
  glm::vec3 scale = tf.GetLocalScale().value_or(glm::vec3(1.0F));

  if (const auto x = settings->GetFloat(prefix + ".position.x")) {
    pos.x = *x;
  }
  if (const auto y = settings->GetFloat(prefix + ".position.y")) {
    pos.y = *y;
  }
  if (const auto z = settings->GetFloat(prefix + ".position.z")) {
    pos.z = *z;
  }

  if (const auto x = settings->GetFloat(prefix + ".rotation.x")) {
    rot.x = *x;
  }
  if (const auto y = settings->GetFloat(prefix + ".rotation.y")) {
    rot.y = *y;
  }
  if (const auto z = settings->GetFloat(prefix + ".rotation.z")) {
    rot.z = *z;
  }
  if (const auto w = settings->GetFloat(prefix + ".rotation.w")) {
    rot.w = *w;
  }

  if (const auto x = settings->GetFloat(prefix + ".scale.x")) {
    scale.x = *x;
  }
  if (const auto y = settings->GetFloat(prefix + ".scale.y")) {
    scale.y = *y;
  }
  if (const auto z = settings->GetFloat(prefix + ".scale.z")) {
    scale.z = *z;
  }

  tf.SetLocalPosition(pos);
  tf.SetLocalRotation(rot);
  tf.SetLocalScale(scale);

  initial_camera_position_ = pos;
  initial_camera_rotation_ = rot;

  if (const auto has_persp
    = settings->GetBool(prefix + ".camera.has_perspective");
    has_persp && *has_persp) {
    if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
      cam_ref) {
      auto& cam = cam_ref->get();
      if (const auto fov
        = settings->GetFloat(prefix + ".camera.perspective.fov")) {
        cam.SetFieldOfView(*fov);
      }
      if (const auto near_plane
        = settings->GetFloat(prefix + ".camera.perspective.near")) {
        cam.SetNearPlane(*near_plane);
      }
      if (const auto far_plane
        = settings->GetFloat(prefix + ".camera.perspective.far")) {
        cam.SetFarPlane(*far_plane);
      }
    }
  }

  if (const auto has_ortho
    = settings->GetBool(prefix + ".camera.has_orthographic");
    has_ortho && *has_ortho) {
    if (auto cam_ref = active_camera_.GetCameraAs<scene::OrthographicCamera>();
      cam_ref) {
      auto& cam = cam_ref->get();
      const auto left = settings->GetFloat(prefix + ".camera.ortho.left");
      const auto right = settings->GetFloat(prefix + ".camera.ortho.right");
      const auto bottom = settings->GetFloat(prefix + ".camera.ortho.bottom");
      const auto top = settings->GetFloat(prefix + ".camera.ortho.top");
      const auto near_plane = settings->GetFloat(prefix + ".camera.ortho.near");
      const auto far_plane = settings->GetFloat(prefix + ".camera.ortho.far");
      if (left && right && bottom && top && near_plane && far_plane) {
        cam.SetExtents(*left, *right, *bottom, *top, *near_plane, *far_plane);
      }
    }
  }

  if (camera_rig_) {
    camera_rig_->SyncFromActiveCamera();
    if (const auto orbit = camera_rig_->GetOrbitController(); orbit) {
      glm::vec3 target = orbit->GetTarget();
      if (const auto x = settings->GetFloat(prefix + ".orbit.target.x")) {
        target.x = *x;
      }
      if (const auto y = settings->GetFloat(prefix + ".orbit.target.y")) {
        target.y = *y;
      }
      if (const auto z = settings->GetFloat(prefix + ".orbit.target.z")) {
        target.z = *z;
      }
      orbit->SetTarget(target);
      if (const auto distance
        = settings->GetFloat(prefix + ".orbit.distance")) {
        orbit->SetDistance(*distance);
      }
      if (const auto mode_str = settings->GetString(prefix + ".orbit.mode")) {
        if (const auto mode = ParseOrbitMode(*mode_str)) {
          orbit->SetMode(*mode);
        }
      }
    }

    if (const auto fly = camera_rig_->GetFlyController(); fly) {
      if (const auto speed = settings->GetFloat(prefix + ".fly.move_speed")) {
        fly->SetMoveSpeed(*speed);
      }
      if (const auto sensitivity
        = settings->GetFloat(prefix + ".fly.look_sensitivity")) {
        fly->SetLookSensitivity(*sensitivity);
      }
      if (const auto boost
        = settings->GetFloat(prefix + ".fly.boost_multiplier")) {
        fly->SetBoostMultiplier(*boost);
      }
      if (const auto plane_lock
        = settings->GetBool(prefix + ".fly.plane_lock")) {
        fly->SetPlaneLockActive(*plane_lock);
      }
    }
  }

  last_saved_state_ = CaptureActiveCameraState();
}

auto CameraLifecycleService::CaptureActiveCameraState() -> PersistedCameraState
{
  PersistedCameraState current;
  if (!active_camera_.IsAlive()) {
    return current;
  }

  const std::string camera_id = active_camera_.GetName();
  if (camera_id.empty()) {
    return current;
  }

  current.valid = true;
  current.camera_id = camera_id;
  current.camera_mode
    = camera_rig_ ? static_cast<int>(camera_rig_->GetMode()) : 0;

  auto tf = active_camera_.GetTransform();
  current.position = tf.GetLocalPosition().value_or(glm::vec3(0.0F));
  current.rotation
    = tf.GetLocalRotation().value_or(glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
  current.scale = tf.GetLocalScale().value_or(glm::vec3(1.0F));

  if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
    cam_ref) {
    const auto& cam = cam_ref->get();
    current.has_perspective = true;
    current.perspective_fov = cam.GetFieldOfView();
    current.perspective_near = cam.GetNearPlane();
    current.perspective_far = cam.GetFarPlane();
  }

  if (auto cam_ref = active_camera_.GetCameraAs<scene::OrthographicCamera>();
    cam_ref) {
    const auto& cam = cam_ref->get();
    current.has_orthographic = true;
    current.ortho_extents = cam.GetExtents();
  }

  if (camera_rig_) {
    if (const auto orbit = camera_rig_->GetOrbitController(); orbit) {
      current.orbit_target = orbit->GetTarget();
      current.orbit_distance = orbit->GetDistance();
      current.orbit_mode = static_cast<int>(orbit->GetMode());
    }
    if (const auto fly = camera_rig_->GetFlyController(); fly) {
      current.fly_move_speed = fly->GetMoveSpeed();
      current.fly_look_sensitivity = fly->GetLookSensitivity();
      current.fly_boost_multiplier = fly->GetBoostMultiplier();
      current.fly_plane_lock = fly->GetPlaneLockActive();
    }
  }

  return current;
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
