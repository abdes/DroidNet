//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/CameraVm.h"
#include "DemoShell/UI/DroneCameraController.h"
#include "DemoShell/UI/FlyCameraController.h"
#include "DemoShell/UI/OrbitCameraController.h"

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

auto CameraVm::IsDroneAvailable() const -> bool
{
  return camera_rig_ && camera_rig_->IsDroneAvailable();
}

auto CameraVm::GetDroneProgress() const -> double
{
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    return camera_rig_->GetDroneController()->GetProgress();
  }
  return 0.0;
}

auto CameraVm::GetDroneSpeed() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneSpeed();
}

auto CameraVm::SetDroneSpeed(float speed) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneSpeed(speed);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetSpeed(speed);
  }
}

auto CameraVm::GetDroneDamping() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneDamping();
}

auto CameraVm::SetDroneDamping(float damping) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneDamping(damping);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetDamping(damping);
  }
}

auto CameraVm::GetDroneFocusHeight() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneFocusHeight();
}

auto CameraVm::SetDroneFocusHeight(float height) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneFocusHeight(height);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetFocusHeight(height);
  }
}

auto CameraVm::GetDroneFocusOffset() -> glm::vec2
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return { service_->GetDroneFocusOffsetX(), service_->GetDroneFocusOffsetY() };
}

auto CameraVm::SetDroneFocusOffset(glm::vec2 offset) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneFocusOffsetX(offset.x);
  service_->SetDroneFocusOffsetY(offset.y);
  epoch_ = service_->GetEpoch();
  // Note: DroneCameraController doesn't have offset setter yet in header API,
  // we might need to add it or just use FocusTarget.
  // Actually, SetFocusTarget takes a vec3. We use offset X/Y and Height for the
  // target.
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetFocusTarget(
      glm::vec3(offset.x, service_->GetDroneFocusHeight(), offset.y));
  }
}

auto CameraVm::GetDroneRunning() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneRunning();
}

auto CameraVm::SetDroneRunning(bool running) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneRunning(running);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    if (running) {
      camera_rig_->GetDroneController()->Start();
    } else {
      camera_rig_->GetDroneController()->Stop();
    }
  }
}

auto CameraVm::GetDroneBobAmplitude() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneBobAmplitude();
}

auto CameraVm::SetDroneBobAmplitude(float amp) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneBobAmplitude(amp);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetBobAmplitude(amp);
  }
}

auto CameraVm::GetDroneBobFrequency() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneBobFrequency();
}

auto CameraVm::SetDroneBobFrequency(float hz) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneBobFrequency(hz);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetBobFrequency(hz);
  }
}

auto CameraVm::GetDroneNoiseAmplitude() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneNoiseAmplitude();
}

auto CameraVm::SetDroneNoiseAmplitude(float amp) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneNoiseAmplitude(amp);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetNoiseAmplitude(amp);
  }
}

auto CameraVm::GetDroneBankFactor() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneBankFactor();
}

auto CameraVm::SetDroneBankFactor(float factor) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneBankFactor(factor);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetBankFactor(factor);
  }
}

auto CameraVm::GetDronePOISlowdownRadius() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDronePOISlowdownRadius();
}

auto CameraVm::SetDronePOISlowdownRadius(float radius) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDronePOISlowdownRadius(radius);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetPOISlowdownRadius(radius);
  }
}

auto CameraVm::GetDronePOIMinSpeed() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDronePOIMinSpeed();
}

auto CameraVm::SetDronePOIMinSpeed(float factor) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDronePOIMinSpeed(factor);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetPOIMinSpeedFactor(factor);
  }
}

auto CameraVm::GetDroneShowPath() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneShowPath();
}

auto CameraVm::SetDroneShowPath(bool show) -> void
{
  std::lock_guard lock(mutex_);
  service_->SetDroneShowPath(show);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetShowPathPreview(show);
  }
}

auto CameraVm::HasActiveCamera() const -> bool
{
  return camera_lifecycle_ && camera_lifecycle_->GetActiveCamera().IsAlive();
}

auto CameraVm::GetActiveCameraNode() const -> std::optional<scene::SceneNode>
{
  if (!camera_lifecycle_) {
    return std::nullopt;
  }
  auto& camera = camera_lifecycle_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return std::nullopt;
  }
  return camera;
}

auto CameraVm::GetCameraPosition() -> glm::vec3
{
  if (!HasActiveCamera()) {
    return glm::vec3(0.0F);
  }
  auto transform = camera_lifecycle_->GetActiveCamera().GetTransform();
  if (auto pos = transform.GetLocalPosition()) {
    return *pos;
  }
  return glm::vec3(0.0F);
}

auto CameraVm::GetCameraRotation() -> glm::quat
{
  if (!HasActiveCamera()) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }
  auto transform = camera_lifecycle_->GetActiveCamera().GetTransform();
  if (auto rot = transform.GetLocalRotation()) {
    return *rot;
  }
  return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
}

auto CameraVm::GetDronePathPoints() const -> std::span<const glm::vec3>
{
  if (camera_rig_) {
    if (const auto drone = camera_rig_->GetDroneController()) {
      return std::span(drone->GetPathPoints());
    }
  }
  static const std::vector<glm::vec3> empty;
  return std::span(empty);
}

auto CameraVm::GetActionStateString(
  const std::shared_ptr<input::Action>& action) const -> const char*
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

auto CameraVm::GetMoveForwardAction() const -> std::shared_ptr<input::Action>
{
  return camera_rig_ ? camera_rig_->GetMoveForwardAction() : nullptr;
}
auto CameraVm::GetMoveBackwardAction() const -> std::shared_ptr<input::Action>
{
  return camera_rig_ ? camera_rig_->GetMoveBackwardAction() : nullptr;
}
auto CameraVm::GetMoveLeftAction() const -> std::shared_ptr<input::Action>
{
  return camera_rig_ ? camera_rig_->GetMoveLeftAction() : nullptr;
}
auto CameraVm::GetMoveRightAction() const -> std::shared_ptr<input::Action>
{
  return camera_rig_ ? camera_rig_->GetMoveRightAction() : nullptr;
}
auto CameraVm::GetFlyBoostAction() const -> std::shared_ptr<input::Action>
{
  return camera_rig_ ? camera_rig_->GetFlyBoostAction() : nullptr;
}
auto CameraVm::GetFlyPlaneLockAction() const -> std::shared_ptr<input::Action>
{
  return camera_rig_ ? camera_rig_->GetFlyPlaneLockAction() : nullptr;
}
auto CameraVm::GetRmbAction() const -> std::shared_ptr<input::Action>
{
  return camera_rig_ ? camera_rig_->GetRmbAction() : nullptr;
}
auto CameraVm::GetOrbitAction() const -> std::shared_ptr<input::Action>
{
  return camera_rig_ ? camera_rig_->GetOrbitAction() : nullptr;
}

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
    if (auto drone = camera_rig_->GetDroneController()) {
      drone->SetSpeed(service_->GetDroneSpeed());
      drone->SetDamping(service_->GetDroneDamping());
      drone->SetFocusHeight(service_->GetDroneFocusHeight());
      drone->SetFocusTarget(glm::vec3(service_->GetDroneFocusOffsetX(),
        service_->GetDroneFocusHeight(), service_->GetDroneFocusOffsetY()));
      drone->SetBobAmplitude(service_->GetDroneBobAmplitude());
      drone->SetBobFrequency(service_->GetDroneBobFrequency());
      drone->SetNoiseAmplitude(service_->GetDroneNoiseAmplitude());
      drone->SetBankFactor(service_->GetDroneBankFactor());
      drone->SetPOISlowdownRadius(service_->GetDronePOISlowdownRadius());
      drone->SetPOIMinSpeedFactor(service_->GetDronePOIMinSpeed());
      drone->SetShowPathPreview(service_->GetDroneShowPath());

      const bool should_run = service_->GetDroneRunning();
      const bool is_running = drone->IsFlying();
      if (should_run && !is_running) {
        drone->Start();
      } else if (!should_run && is_running) {
        drone->Stop();
      }
    }
  }
}

auto CameraVm::IsStale() const -> bool
{
  return epoch_ != service_->GetEpoch();
}

} // namespace oxygen::examples::ui
