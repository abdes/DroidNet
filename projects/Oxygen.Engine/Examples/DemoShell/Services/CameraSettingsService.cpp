//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>
#include <Oxygen/Renderer/Pipeline/RenderingPipeline.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>

#include "DemoShell/Services/CameraSettingsService.h"
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
    const glm::vec3& target, const glm::vec3& up_direction = space::move::Up)
    -> glm::quat
  {
    // NOLINTBEGIN(*-magic-numbers)
    const auto forward_raw = target - position;
    const float forward_len2 = glm::dot(forward_raw, forward_raw);
    if (forward_len2 <= 1e-8F) {
      return { 1.0F, 0.0F, 0.0F, 0.0F };
    }

    const auto forward = glm::normalize(forward_raw);
    // Avoid singularities when forward is colinear with up.
    glm::vec3 up_dir = up_direction;
    const float dot_abs = std::abs(glm::dot(forward, glm::normalize(up_dir)));
    if (dot_abs > 0.999F) {
      // Pick an alternate up that is guaranteed to be non-colinear.
      up_dir
        = (std::abs(forward.z) > 0.9F) ? space::move::Back : space::move::Up;
    }

    const auto right_raw = glm::cross(forward, up_dir);
    const float right_len2 = glm::dot(right_raw, right_raw);
    if (right_len2 <= std::numeric_limits<float>::epsilon()) {
      return { 1.0F, 0.0F, 0.0F, 0.0F };
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
    // NOLINTEND(*-magic-numbers)
  }

} // namespace

auto CameraSettingsService::GetCameraControlMode() const -> CameraControlMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  if (!active_camera_id_.empty()) {
    const std::string prefix = "camera_rig." + active_camera_id_;
    if (const auto value = settings->GetString(prefix + ".mode")) {
      if (*value == "fly") {
        return CameraControlMode::kFly;
      }
      if (*value == "drone") {
        return CameraControlMode::kDrone;
      }
      return CameraControlMode::kOrbit;
    }
  }
  return CameraControlMode::kOrbit;
}

auto CameraSettingsService::SetCameraControlMode(CameraControlMode mode) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  if (active_camera_id_.empty()) {
    return;
  }

  const std::string prefix = "camera_rig." + active_camera_id_;
  const auto* value = "orbit";
  if (mode == CameraControlMode::kFly) {
    value = "fly";
  } else if (mode == CameraControlMode::kDrone) {
    value = "drone";
  }
  settings->SetString(prefix + ".mode", value);
  ++epoch_;
}

auto CameraSettingsService::SetActiveCameraId(std::string_view camera_id)
  -> void
{
  const std::string id(camera_id);
  if (active_camera_id_ == id) {
    return;
  }
  active_camera_id_ = id;
  ++epoch_;
}

auto CameraSettingsService::BindCameraRig(
  observer_ptr<ui::CameraRigController> rig) -> void
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

auto CameraSettingsService::OnFrameStart(
  const engine::FrameContext& /*context*/) -> void
{
}

auto CameraSettingsService::OnSceneActivated(scene::Scene& /*scene*/) -> void
{
  active_camera_ = {};
  pending_sync_ = false;
  pending_reset_ = false;
  if (camera_rig_) {
    camera_rig_->SetActiveCamera(nullptr);
  }
  last_saved_state_ = {};
  active_camera_id_.clear();
  ++epoch_;
}

auto CameraSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/,
  const renderer::CompositionView& view) -> void
{
  if (!view.camera.has_value()) {
    DCHECK_F(false, "Main view must provide a camera");
    return;
  }

  auto camera = view.camera.value();
  if (!camera.IsAlive()) {
    DCHECK_F(false, "Main view camera must be alive");
    return;
  }

  if (!camera.HasCamera()) {
    DCHECK_F(false, "Main view camera must have a camera component");
    return;
  }

  const bool camera_changed = !active_camera_.IsAlive()
    || active_camera_.GetHandle() != camera.GetHandle();
  if (camera_changed) {
    SetActiveCamera(camera);
  }

  const auto& viewport = view.view.viewport;
  if (viewport.width > 0.0F && viewport.height > 0.0F) {
    const float aspect
      = viewport.height > 0.0F ? (viewport.width / viewport.height) : 1.0F;
    ApplyViewportToActive(aspect, viewport);
  }

  ApplyPendingSync();
  ApplyPendingReset();
}

auto CameraSettingsService::GetOrbitMode() const -> OrbitMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  if (const auto value = settings->GetString(kOrbitModeKey)) {
    if (*value == "trackball") {
      return OrbitMode::kTrackball;
    }
  }
  return OrbitMode::kTurntable;
}

auto CameraSettingsService::SetOrbitMode(OrbitMode mode) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  const char* value
    = (mode == OrbitMode::kTrackball) ? "trackball" : "turntable";
  settings->SetString(kOrbitModeKey, value);
  ++epoch_;
}

auto CameraSettingsService::GetFlyMoveSpeed() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  if (const auto value = settings->GetFloat(kFlyMoveSpeedKey)) {
    return *value;
  }
  return 5.0F;
}

auto CameraSettingsService::SetFlyMoveSpeed(float speed) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  settings->SetFloat(kFlyMoveSpeedKey, speed);
  ++epoch_;
}

auto CameraSettingsService::GetDroneSpeed() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 6.0F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneSpeedKey).value_or(6.0F);
}

auto CameraSettingsService::SetDroneSpeed(float speed) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneSpeedKey, speed);
  ++epoch_;
}

auto CameraSettingsService::GetDroneDamping() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 8.0F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneDampingKey).value_or(8.0F);
}

auto CameraSettingsService::SetDroneDamping(float damping) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneDampingKey, damping);
  ++epoch_;
}

auto CameraSettingsService::GetDroneFocusHeight() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 0.8F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneFocusHeightKey).value_or(0.8F);
}

auto CameraSettingsService::SetDroneFocusHeight(float height) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneFocusHeightKey, height);
  ++epoch_;
}

auto CameraSettingsService::GetDroneFocusOffsetX() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 0.0F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneFocusOffsetXKey).value_or(0.0F);
}

auto CameraSettingsService::SetDroneFocusOffsetX(float offset) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneFocusOffsetXKey, offset);
  ++epoch_;
}

auto CameraSettingsService::GetDroneFocusOffsetY() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 0.0F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneFocusOffsetYKey).value_or(0.0F);
}

auto CameraSettingsService::SetDroneFocusOffsetY(float offset) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneFocusOffsetYKey, offset);
  ++epoch_;
}

auto CameraSettingsService::GetDroneRunning() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return true;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetBool(prefix + kDroneRunningKey).value_or(true);
}

auto CameraSettingsService::SetDroneRunning(bool running) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetBool(prefix + kDroneRunningKey, running);
  ++epoch_;
}

auto CameraSettingsService::GetDroneBobAmplitude() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 0.06F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneBobAmpKey).value_or(0.06F);
}

auto CameraSettingsService::SetDroneBobAmplitude(float amp) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneBobAmpKey, amp);
  ++epoch_;
}

auto CameraSettingsService::GetDroneBobFrequency() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 1.6F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneBobFreqKey).value_or(1.6F);
}

auto CameraSettingsService::SetDroneBobFrequency(float hz) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneBobFreqKey, hz);
  ++epoch_;
}

auto CameraSettingsService::GetDroneNoiseAmplitude() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 0.03F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneNoiseAmpKey).value_or(0.03F);
}

auto CameraSettingsService::SetDroneNoiseAmplitude(float amp) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneNoiseAmpKey, amp);
  ++epoch_;
}

auto CameraSettingsService::GetDroneBankFactor() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 0.045F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneBankFactorKey).value_or(0.045F);
}

auto CameraSettingsService::SetDroneBankFactor(float factor) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDroneBankFactorKey, factor);
  ++epoch_;
}

auto CameraSettingsService::GetDronePOISlowdownRadius() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 3.0F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDronePOIRadiusKey).value_or(3.0F);
}

auto CameraSettingsService::SetDronePOISlowdownRadius(float radius) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDronePOIRadiusKey, radius);
  ++epoch_;
}

auto CameraSettingsService::GetDronePOIMinSpeed() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return 0.3F;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDronePOIMinSpeedKey).value_or(0.3F);
}

auto CameraSettingsService::SetDronePOIMinSpeed(float factor) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetFloat(prefix + kDronePOIMinSpeedKey, factor);
  ++epoch_;
}

auto CameraSettingsService::GetDroneShowPath() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (active_camera_id_.empty()) {
    return false;
  }
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetBool(prefix + kDroneShowPathKey).value_or(false);
}

auto CameraSettingsService::SetDroneShowPath(bool show) -> void
{
  if (active_camera_id_.empty()) {
    return;
  }
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  settings->SetBool(prefix + kDroneShowPathKey, show);
  ++epoch_;
}

void CameraSettingsService::SetActiveCamera(scene::SceneNode camera)
{
  active_camera_ = std::move(camera);
  if (camera_rig_) {
    camera_rig_->SetActiveCamera(observer_ptr { &active_camera_ });
  }

  const auto camera_id = active_camera_.GetName();
  if (!camera_id.empty()) {
    SetActiveCameraId(camera_id);
  }

  // Capture scene-authored/default camera pose before any persisted overrides.
  // Reset should always have a safe baseline even when persisted state is bad.
  CaptureInitialPose();
  const bool restored_transform = RestoreActiveCameraSettings();
  if (!restored_transform) {
    EnsureFlyCameraFacingScene();
  }
  RequestSyncFromActive();
}

void CameraSettingsService::CaptureInitialPose()
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

void CameraSettingsService::EnsureFlyCameraFacingScene()
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
  if (len2 <= std::numeric_limits<float>::epsilon()) {
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

void CameraSettingsService::RequestSyncFromActive()
{
  pending_sync_ = true;
  ++epoch_;
}

void CameraSettingsService::ApplyPendingSync()
{
  if (!pending_sync_ || !active_camera_.IsAlive()) {
    return;
  }

  if (camera_rig_) {
    camera_rig_->SyncFromActiveCamera();
  }

  pending_sync_ = false;
}

auto CameraSettingsService::RequestReset() -> void
{
  pending_reset_ = true;
  ++epoch_;
}

void CameraSettingsService::ApplyPendingReset()
{
  if (!pending_reset_ || !active_camera_.IsAlive()) {
    return;
  }

  auto transform = active_camera_.GetTransform();
  glm::vec3 reset_position = initial_camera_position_;
  glm::quat reset_rotation = initial_camera_rotation_;

  const bool orbit_mode
    = camera_rig_ && camera_rig_->GetMode() == ui::CameraControlMode::kOrbit;
  if (orbit_mode) {
    constexpr glm::vec3 orbit_target(0.0F, 0.0F, 0.0F);
    float orbit_distance = glm::length(initial_camera_position_ - orbit_target);
    if (!std::isfinite(orbit_distance) || orbit_distance < 1.0F) {
      orbit_distance = 15.0F;
      reset_position = orbit_target - space::look::Forward * orbit_distance;
    } else {
      // Keep the baseline direction, but enforce a valid orbit radius.
      const glm::vec3 baseline_dir
        = glm::normalize(initial_camera_position_ - orbit_target);
      reset_position = orbit_target + baseline_dir * orbit_distance;
    }
    reset_rotation = MakeLookRotationFromPosition(reset_position, orbit_target);

    if (camera_rig_) {
      if (const auto orbit = camera_rig_->GetOrbitController(); orbit) {
        orbit->SetTarget(orbit_target);
        orbit->SetDistance(orbit_distance);
      }
    }
  }

  transform.SetLocalPosition(reset_position);
  transform.SetLocalRotation(reset_rotation);

  if (camera_rig_) {
    camera_rig_->SyncFromActiveCamera();
  }

  pending_reset_ = false;
  LOG_F(INFO, "Camera reset to initial pose");
}

auto CameraSettingsService::PersistedCameraState::TransformState::IsDirty(
  const TransformState& other) const -> bool
{
  return !NearlyEqual(position, other.position)
    || !NearlyEqual(rotation, other.rotation);
}

void CameraSettingsService::PersistedCameraState::TransformState::Persist(
  SettingsService& settings, const std::string& prefix) const
{
  settings.SetFloat(prefix + ".position.x", position.x);
  settings.SetFloat(prefix + ".position.y", position.y);
  settings.SetFloat(prefix + ".position.z", position.z);

  settings.SetFloat(prefix + ".rotation.x", rotation.x);
  settings.SetFloat(prefix + ".rotation.y", rotation.y);
  settings.SetFloat(prefix + ".rotation.z", rotation.z);
  settings.SetFloat(prefix + ".rotation.w", rotation.w);
}

auto CameraSettingsService::PersistedCameraState::PerspectiveState::IsDirty(
  const PerspectiveState& other) const -> bool
{
  return enabled != other.enabled
    || (enabled
      && (!NearlyEqual(fov, other.fov)
        || !NearlyEqual(near_plane, other.near_plane)
        || !NearlyEqual(far_plane, other.far_plane)));
}

void CameraSettingsService::PersistedCameraState::PerspectiveState::Persist(
  SettingsService& settings, const std::string& prefix) const
{
  settings.SetBool(prefix + ".camera.has_perspective", enabled);
  if (!enabled) {
    return;
  }

  settings.SetFloat(prefix + ".camera.perspective.fov", fov);
  settings.SetFloat(prefix + ".camera.perspective.near", near_plane);
  settings.SetFloat(prefix + ".camera.perspective.far", far_plane);
}

auto CameraSettingsService::PersistedCameraState::OrthoState::IsDirty(
  const OrthoState& other) const -> bool
{
  return enabled != other.enabled
    || (enabled
      && !std::equal(extents.begin(), extents.end(), other.extents.begin(),
        [](
          const float lhs, const float rhs) { return NearlyEqual(lhs, rhs); }));
}

void CameraSettingsService::PersistedCameraState::OrthoState::Persist(
  SettingsService& settings, const std::string& prefix) const
{
  settings.SetBool(prefix + ".camera.has_orthographic", enabled);
  if (!enabled) {
    return;
  }

  settings.SetFloat(prefix + ".camera.ortho.left", extents[0]);
  settings.SetFloat(prefix + ".camera.ortho.right", extents[1]);
  settings.SetFloat(prefix + ".camera.ortho.bottom", extents[2]);
  settings.SetFloat(prefix + ".camera.ortho.top", extents[3]);
  settings.SetFloat(prefix + ".camera.ortho.near", extents[4]);
  // NOLINTNEXTLINE(*-magic-numbers)
  settings.SetFloat(prefix + ".camera.ortho.far", extents[5]);
}

auto CameraSettingsService::PersistedCameraState::ExposureState::IsDirty(
  const ExposureState& other) const -> bool
{
  return enabled != other.enabled
    || (enabled
      && (!NearlyEqual(aperture_f, other.aperture_f)
        || !NearlyEqual(shutter_rate, other.shutter_rate)
        || !NearlyEqual(iso, other.iso)));
}

void CameraSettingsService::PersistedCameraState::ExposureState::Persist(
  SettingsService& settings, const std::string& prefix) const
{
  settings.SetBool(prefix + ".camera.exposure.enabled", enabled);
  if (!enabled) {
    return;
  }

  settings.SetFloat(prefix + ".camera.exposure.aperture_f", aperture_f);
  settings.SetFloat(prefix + ".camera.exposure.shutter_rate", shutter_rate);
  settings.SetFloat(prefix + ".camera.exposure.iso", iso);
}

auto CameraSettingsService::PersistedCameraState::IsSameCamera(
  const PersistedCameraState& other) const -> bool
{
  return valid && other.valid && camera_id == other.camera_id;
}

auto CameraSettingsService::PersistActiveCameraSettings() -> void
{
  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return;
  }
  const auto current = CaptureActiveCameraState();
  if (!current.valid) {
    return;
  }

  const bool same_camera = last_saved_state_.IsSameCamera(current);
  const bool mode_dirty
    = !same_camera || last_saved_state_.camera_mode != current.camera_mode;
  const bool transform_dirty
    = !same_camera || current.transform.IsDirty(last_saved_state_.transform);
  const bool perspective_dirty = !same_camera
    || current.perspective.IsDirty(last_saved_state_.perspective);
  const bool ortho_dirty
    = !same_camera || current.ortho.IsDirty(last_saved_state_.ortho);
  const bool orbit_dirty = !same_camera
    || !NearlyEqual(last_saved_state_.orbit_target, current.orbit_target)
    || !NearlyEqual(last_saved_state_.orbit_distance, current.orbit_distance)
    || last_saved_state_.orbit_mode != current.orbit_mode;
  const bool fly_dirty = !same_camera
    || !NearlyEqual(last_saved_state_.fly_move_speed, current.fly_move_speed)
    || !NearlyEqual(
      last_saved_state_.fly_look_sensitivity, current.fly_look_sensitivity)
    || !NearlyEqual(
      last_saved_state_.fly_boost_multiplier, current.fly_boost_multiplier)
    || last_saved_state_.fly_plane_lock != current.fly_plane_lock;
  const bool exposure_dirty
    = !same_camera || current.exposure.IsDirty(last_saved_state_.exposure);
  const bool unchanged = !mode_dirty && !transform_dirty && !perspective_dirty
    && !ortho_dirty && !orbit_dirty && !fly_dirty && !exposure_dirty;

  if (unchanged) {
    return;
  }

  const std::string prefix = "camera_rig." + current.camera_id;

  if (mode_dirty) {
    settings->SetString(prefix + ".mode",
      std::string(CameraModeToString(
        static_cast<ui::CameraControlMode>(current.camera_mode))));
  }

  if (transform_dirty) {
    current.transform.Persist(*settings, prefix);
  }

  if (perspective_dirty) {
    current.perspective.Persist(*settings, prefix);
  }

  if (ortho_dirty) {
    current.ortho.Persist(*settings, prefix);
  }

  if (orbit_dirty) {
    settings->SetFloat(prefix + ".orbit.target.x", current.orbit_target.x);
    settings->SetFloat(prefix + ".orbit.target.y", current.orbit_target.y);
    settings->SetFloat(prefix + ".orbit.target.z", current.orbit_target.z);
    settings->SetFloat(prefix + ".orbit.distance", current.orbit_distance);
    settings->SetString(prefix + ".orbit.mode",
      std::string(
        OrbitModeToString(static_cast<ui::OrbitMode>(current.orbit_mode))));
  }

  if (fly_dirty) {
    settings->SetFloat(prefix + ".fly.move_speed", current.fly_move_speed);
    settings->SetFloat(
      prefix + ".fly.look_sensitivity", current.fly_look_sensitivity);
    settings->SetFloat(
      prefix + ".fly.boost_multiplier", current.fly_boost_multiplier);
    settings->SetBool(prefix + ".fly.plane_lock", current.fly_plane_lock);
  }

  if (exposure_dirty) {
    current.exposure.Persist(*settings, prefix);
  }

  last_saved_state_ = current;
}

auto CameraSettingsService::RestoreActiveCameraSettings() -> bool
{
  if (!active_camera_.IsAlive()) {
    return false;
  }

  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return false;
  }

  const std::string camera_id = active_camera_.GetName();
  if (camera_id.empty()) {
    return false;
  }

  const std::string prefix = "camera_rig." + camera_id;
  std::string mode_label = "default";
  std::string orbit_mode_label = "default";

  if (camera_rig_) {
    if (const auto mode_str = settings->GetString(prefix + ".mode")) {
      if (const auto mode = ParseCameraMode(*mode_str)) {
        camera_rig_->SetMode(*mode);
        mode_label = *mode_str;
      }
    }
  }

  auto tf = active_camera_.GetTransform();
  glm::vec3 pos = tf.GetLocalPosition().value_or(glm::vec3(0.0F));
  glm::quat rot
    = tf.GetLocalRotation().value_or(glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
  bool restored_transform = false;
  glm::vec3 orbit_target(0.0F);
  bool orbit_target_loaded = false;
  const auto orbit_distance = settings->GetFloat(prefix + ".orbit.distance");
  if (const auto x = settings->GetFloat(prefix + ".orbit.target.x")) {
    orbit_target.x = *x;
    orbit_target_loaded = true;
  }
  if (const auto y = settings->GetFloat(prefix + ".orbit.target.y")) {
    orbit_target.y = *y;
    orbit_target_loaded = true;
  }
  if (const auto z = settings->GetFloat(prefix + ".orbit.target.z")) {
    orbit_target.z = *z;
    orbit_target_loaded = true;
  }

  if (const auto x = settings->GetFloat(prefix + ".position.x")) {
    pos.x = *x;
    restored_transform = true;
  }
  if (const auto y = settings->GetFloat(prefix + ".position.y")) {
    pos.y = *y;
    restored_transform = true;
  }
  if (const auto z = settings->GetFloat(prefix + ".position.z")) {
    pos.z = *z;
    restored_transform = true;
  }

  if (const auto x = settings->GetFloat(prefix + ".rotation.x")) {
    rot.x = *x;
    restored_transform = true;
  }
  if (const auto y = settings->GetFloat(prefix + ".rotation.y")) {
    rot.y = *y;
    restored_transform = true;
  }
  if (const auto z = settings->GetFloat(prefix + ".rotation.z")) {
    rot.z = *z;
    restored_transform = true;
  }
  if (const auto w = settings->GetFloat(prefix + ".rotation.w")) {
    rot.w = *w;
    restored_transform = true;
  }

  const bool is_orbit_mode = (mode_label == "orbit")
    || (camera_rig_ && camera_rig_->GetMode() == ui::CameraControlMode::kOrbit);
  if (is_orbit_mode && orbit_distance.has_value() && orbit_target_loaded) {
    const glm::vec3 forward = rot * space::look::Forward;
    pos = orbit_target - forward * (*orbit_distance);
    restored_transform = true;
  }

  tf.SetLocalPosition(pos);
  tf.SetLocalRotation(rot);

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

      auto& exposure = cam.Exposure();
      if (const auto enabled
        = settings->GetBool(prefix + ".camera.exposure.enabled")) {
        if (*enabled) {
          if (const auto aperture_f
            = settings->GetFloat(prefix + ".camera.exposure.aperture_f")) {
            exposure.aperture_f = *aperture_f;
          }
          if (const auto shutter_rate
            = settings->GetFloat(prefix + ".camera.exposure.shutter_rate")) {
            exposure.shutter_rate = *shutter_rate;
          }
          if (const auto iso
            = settings->GetFloat(prefix + ".camera.exposure.iso")) {
            exposure.iso = *iso;
          }
        }
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

      auto& exposure = cam.Exposure();
      if (const auto enabled
        = settings->GetBool(prefix + ".camera.exposure.enabled")) {
        if (*enabled) {
          if (const auto aperture_f
            = settings->GetFloat(prefix + ".camera.exposure.aperture_f")) {
            exposure.aperture_f = *aperture_f;
          }
          if (const auto shutter_rate
            = settings->GetFloat(prefix + ".camera.exposure.shutter_rate")) {
            exposure.shutter_rate = *shutter_rate;
          }
          if (const auto iso
            = settings->GetFloat(prefix + ".camera.exposure.iso")) {
            exposure.iso = *iso;
          }
        }
      }
    }
  }

  if (camera_rig_) {
    if (const auto orbit = camera_rig_->GetOrbitController(); orbit) {
      if (orbit_target_loaded) {
        orbit->SetTarget(orbit_target);
      }
      if (orbit_distance.has_value()) {
        orbit->SetDistance(*orbit_distance);
      }
      if (const auto mode_str = settings->GetString(prefix + ".orbit.mode")) {
        if (const auto mode = ParseOrbitMode(*mode_str)) {
          orbit->SetMode(*mode);
          orbit_mode_label = *mode_str;
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

    camera_rig_->SyncFromActiveCamera();
  }

  last_saved_state_ = CaptureActiveCameraState();
  return restored_transform;
}

auto CameraSettingsService::CaptureActiveCameraState() -> PersistedCameraState
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
  current.transform.position = tf.GetLocalPosition().value_or(glm::vec3(0.0F));
  current.transform.rotation
    = tf.GetLocalRotation().value_or(glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
  current.transform.scale = tf.GetLocalScale().value_or(glm::vec3(1.0F));

  if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
    cam_ref) {
    const auto& cam = cam_ref->get();
    current.perspective.enabled = true;
    current.perspective.fov = cam.GetFieldOfView();
    current.perspective.near_plane = cam.GetNearPlane();
    current.perspective.far_plane = cam.GetFarPlane();

    const auto& exposure = cam.Exposure();
    current.exposure.enabled = true;
    current.exposure.aperture_f = exposure.aperture_f;
    current.exposure.shutter_rate = exposure.shutter_rate;
    current.exposure.iso = exposure.iso;
  }

  if (auto cam_ref = active_camera_.GetCameraAs<scene::OrthographicCamera>();
    cam_ref) {
    const auto& cam = cam_ref->get();
    current.ortho.enabled = true;
    current.ortho.extents = cam.GetExtents();

    const auto& exposure = cam.Exposure();
    current.exposure.enabled = true;
    current.exposure.aperture_f = exposure.aperture_f;
    current.exposure.shutter_rate = exposure.shutter_rate;
    current.exposure.iso = exposure.iso;
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

void CameraSettingsService::ApplyViewportToActive(
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
}

auto CameraSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

} // namespace oxygen::examples
