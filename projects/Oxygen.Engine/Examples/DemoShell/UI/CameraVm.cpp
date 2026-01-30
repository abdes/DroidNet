//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/UI/CameraVm.h"
#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/FlyCameraController.h"
#include "DemoShell/UI/OrbitCameraController.h"
#include <Oxygen/Input/Action.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

CameraVm::CameraVm(observer_ptr<CameraSettingsService> service,
  observer_ptr<CameraLifecycleService> camera_lifecycle,
  observer_ptr<CameraRigController> camera_rig)
  : service_(service)
  , camera_lifecycle_(camera_lifecycle)
  , camera_rig_(camera_rig)
{
  Refresh();
}

auto CameraVm::GetControlMode() -> CameraControlMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return control_mode_;
}

auto CameraVm::SetControlMode(CameraControlMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (control_mode_ == mode) {
    return;
  }

  control_mode_ = mode;
  service_->SetCameraControlMode(mode);
  epoch_ = service_->GetEpoch();

  if (camera_rig_) {
    camera_rig_->SetMode(mode);
  }
}

auto CameraVm::GetOrbitMode() -> OrbitMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return orbit_mode_;
}

auto CameraVm::SetOrbitMode(OrbitMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (orbit_mode_ == mode) {
    return;
  }

  orbit_mode_ = mode;
  service_->SetOrbitMode(mode);
  epoch_ = service_->GetEpoch();

  if (camera_rig_ && camera_rig_->GetOrbitController()) {
    camera_rig_->GetOrbitController()->SetMode(mode);
  }
}

auto CameraVm::GetFlyMoveSpeed() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return fly_move_speed_;
}

auto CameraVm::SetFlyMoveSpeed(float speed) -> void
{
  std::lock_guard lock(mutex_);
  if (fly_move_speed_ == speed) {
    return;
  }

  fly_move_speed_ = speed;
  service_->SetFlyMoveSpeed(speed);
  epoch_ = service_->GetEpoch();

  if (camera_rig_ && camera_rig_->GetFlyController()) {
    camera_rig_->GetFlyController()->SetMoveSpeed(speed);
  }
}

auto CameraVm::HasActiveCamera() const -> bool
{
  return camera_lifecycle_ && camera_lifecycle_->GetActiveCamera().IsAlive();
}

auto CameraVm::GetCameraPosition() -> glm::vec3
{
  if (!HasActiveCamera()) {
    return glm::vec3(0.0f);
  }
  auto transform = camera_lifecycle_->GetActiveCamera().GetTransform();
  if (auto pos = transform.GetLocalPosition()) {
    return *pos;
  }
  return glm::vec3(0.0f);
}

auto CameraVm::GetCameraRotation() -> glm::quat
{
  if (!HasActiveCamera()) {
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  }
  auto transform = camera_lifecycle_->GetActiveCamera().GetTransform();
  if (auto rot = transform.GetLocalRotation()) {
    return *rot;
  }
  return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

auto CameraVm::GetActionStateString(const std::shared_ptr<input::Action>& action) const -> const char*
{
  if (!action) {
    return "<null>";
  }

  if (action->WasCanceledThisFrame()) {
    return "Canceled";
  }
  if (action->WasCompletedThisFrame()) {
    return "Completed";
  }
  if (action->WasTriggeredThisFrame()) {
    return "Triggered";
  }
  if (action->WasReleasedThisFrame()) {
    return "Released";
  }
  if (action->IsOngoing()) {
    return "Ongoing";
  }
  if (action->WasValueUpdatedThisFrame()) {
    return "Updated";
  }

  return "Idle";
}

auto CameraVm::GetMoveForwardAction() const -> std::shared_ptr<input::Action> { return camera_rig_ ? camera_rig_->GetMoveForwardAction() : nullptr; }
auto CameraVm::GetMoveBackwardAction() const -> std::shared_ptr<input::Action> { return camera_rig_ ? camera_rig_->GetMoveBackwardAction() : nullptr; }
auto CameraVm::GetMoveLeftAction() const -> std::shared_ptr<input::Action> { return camera_rig_ ? camera_rig_->GetMoveLeftAction() : nullptr; }
auto CameraVm::GetMoveRightAction() const -> std::shared_ptr<input::Action> { return camera_rig_ ? camera_rig_->GetMoveRightAction() : nullptr; }
auto CameraVm::GetFlyBoostAction() const -> std::shared_ptr<input::Action> { return camera_rig_ ? camera_rig_->GetFlyBoostAction() : nullptr; }
auto CameraVm::GetFlyPlaneLockAction() const -> std::shared_ptr<input::Action> { return camera_rig_ ? camera_rig_->GetFlyPlaneLockAction() : nullptr; }
auto CameraVm::GetRmbAction() const -> std::shared_ptr<input::Action> { return camera_rig_ ? camera_rig_->GetRmbAction() : nullptr; }
auto CameraVm::GetOrbitAction() const -> std::shared_ptr<input::Action> { return camera_rig_ ? camera_rig_->GetOrbitAction() : nullptr; }

auto CameraVm::RequestReset() -> void
{
  if (camera_lifecycle_) {
    camera_lifecycle_->RequestReset();
  }
}

auto CameraVm::PersistActiveCameraSettings() -> void
{
  if (camera_lifecycle_) {
    camera_lifecycle_->PersistActiveCameraSettings();
  }
}

auto CameraVm::Refresh() -> void
{
  control_mode_ = service_->GetCameraControlMode();
  orbit_mode_ = service_->GetOrbitMode();
  fly_move_speed_ = service_->GetFlyMoveSpeed();
  epoch_ = service_->GetEpoch();

  // Also apply to controller on initial refresh if available
  if (camera_rig_) {
    camera_rig_->SetMode(control_mode_);
    if (auto orbit = camera_rig_->GetOrbitController()) {
      orbit->SetMode(orbit_mode_);
    }
    if (auto fly = camera_rig_->GetFlyController()) {
      fly->SetMoveSpeed(fly_move_speed_);
    }
  }
}

auto CameraVm::IsStale() const -> bool
{
  return epoch_ != service_->GetEpoch();
}

} // namespace oxygen::examples::ui
