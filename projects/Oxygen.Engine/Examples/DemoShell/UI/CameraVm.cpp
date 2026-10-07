//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/CameraVm.h"
#include "DemoShell/UI/DroneCameraController.h"
#include "DemoShell/UI/FlyCameraController.h"
#include "DemoShell/UI/OrbitCameraController.h"
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

namespace {
  // Near and far planes in OrthographicCamera::GetExtents().
  constexpr std::size_t kOrthoNearIndex = 4U;
  constexpr std::size_t kOrthoFarIndex = 5U;
} // namespace

CameraVm::CameraVm(observer_ptr<CameraSettingsService> service,
  observer_ptr<CameraRigController> camera_rig)
  : service_(service)
  , camera_rig_(camera_rig)
{
  Refresh();
}

auto CameraVm::GetControlMode() -> CameraControlMode
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return control_mode_;
}

auto CameraVm::SetControlMode(CameraControlMode mode) -> void
{
  std::scoped_lock lock(mutex_);
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
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return orbit_mode_;
}

auto CameraVm::SetOrbitMode(OrbitMode mode) -> void
{
  std::scoped_lock lock(mutex_);
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
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return fly_move_speed_;
}

auto CameraVm::SetFlyMoveSpeed(float speed) -> void
{
  std::scoped_lock lock(mutex_);
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
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneSpeed();
}

auto CameraVm::SetDroneSpeed(float speed) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneSpeed(speed);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetSpeed(speed);
  }
}

auto CameraVm::GetDroneDamping() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneDamping();
}

auto CameraVm::SetDroneDamping(float damping) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneDamping(damping);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetDamping(damping);
  }
}

auto CameraVm::GetDroneFocusHeight() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneFocusHeight();
}

auto CameraVm::SetDroneFocusHeight(float height) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneFocusHeight(height);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetFocusHeight(height);
  }
}

auto CameraVm::GetDroneFocusOffset() -> glm::vec2
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return { service_->GetDroneFocusOffsetX(), service_->GetDroneFocusOffsetY() };
}

auto CameraVm::SetDroneFocusOffset(glm::vec2 offset) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneFocusOffsetX(offset.x);
  service_->SetDroneFocusOffsetY(offset.y);
  epoch_ = service_->GetEpoch();
  // Note: DroneCameraController doesn't have offset setter yet in header API,
  // we might need to add it or just use FocusTarget.
  // Actually, SetFocusTarget takes a vec3. We use offset X/Y and Height for the
  // target.
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetFocusTarget(
      glm::vec3(offset.x, offset.y, service_->GetDroneFocusHeight()));
  }
}

auto CameraVm::GetDroneRunning() -> bool
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneRunning();
}

auto CameraVm::SetDroneRunning(bool running) -> void
{
  std::scoped_lock lock(mutex_);
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
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneBobAmplitude();
}

auto CameraVm::SetDroneBobAmplitude(float amp) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneBobAmplitude(amp);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetBobAmplitude(amp);
  }
}

auto CameraVm::GetDroneBobFrequency() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneBobFrequency();
}

auto CameraVm::SetDroneBobFrequency(float hz) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneBobFrequency(hz);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetBobFrequency(hz);
  }
}

auto CameraVm::GetDroneNoiseAmplitude() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneNoiseAmplitude();
}

auto CameraVm::SetDroneNoiseAmplitude(float amp) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneNoiseAmplitude(amp);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetNoiseAmplitude(amp);
  }
}

auto CameraVm::GetDroneBankFactor() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneBankFactor();
}

auto CameraVm::SetDroneBankFactor(float factor) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneBankFactor(factor);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetBankFactor(factor);
  }
}

auto CameraVm::GetDronePOISlowdownRadius() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDronePOISlowdownRadius();
}

auto CameraVm::SetDronePOISlowdownRadius(float radius) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDronePOISlowdownRadius(radius);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetPOISlowdownRadius(radius);
  }
}

auto CameraVm::GetDronePOIMinSpeed() -> float
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDronePOIMinSpeed();
}

auto CameraVm::SetDronePOIMinSpeed(float factor) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDronePOIMinSpeed(factor);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetPOIMinSpeedFactor(factor);
  }
}

auto CameraVm::GetDroneShowPath() -> bool
{
  std::scoped_lock lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return service_->GetDroneShowPath();
}

auto CameraVm::SetDroneShowPath(bool show) -> void
{
  std::scoped_lock lock(mutex_);
  service_->SetDroneShowPath(show);
  epoch_ = service_->GetEpoch();
  if (camera_rig_ && camera_rig_->GetDroneController()) {
    camera_rig_->GetDroneController()->SetShowPathPreview(show);
  }
}

auto CameraVm::HasActiveCamera() const -> bool
{
  return service_ && service_->GetActiveCamera().IsAlive();
}

auto CameraVm::GetCameraPosition() -> glm::vec3
{
  if (!HasActiveCamera()) {
    return glm::vec3(0.0F);
  }
  auto transform = service_->GetActiveCamera().GetTransform();
  if (auto pos = transform.GetLocalPosition()) {
    return *pos;
  }
  return glm::vec3(0.0F);
}

auto CameraVm::GetCameraRotation() -> glm::quat
{
  if (!HasActiveCamera()) {
    return { 1.0F, 0.0F, 0.0F, 0.0F };
  }
  auto transform = service_->GetActiveCamera().GetTransform();
  if (auto rot = transform.GetLocalRotation()) {
    return *rot;
  }
  return { 1.0F, 0.0F, 0.0F, 0.0F };
}

auto CameraVm::HasPerspectiveCamera() const -> bool
{
  if (!service_) {
    return false;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return false;
  }
  return camera.GetCameraAs<scene::PerspectiveCamera>().has_value();
}

auto CameraVm::HasOrthographicCamera() const -> bool
{
  if (!service_) {
    return false;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return false;
  }
  return camera.GetCameraAs<scene::OrthographicCamera>().has_value();
}

auto CameraVm::GetPerspectiveFovDegrees() const -> float
{
  if (!service_) {
    return 0.0F;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return 0.0F;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    return glm::degrees(cam_ref->get().GetFieldOfView());
  }
  return 0.0F;
}

auto CameraVm::SetPerspectiveFovDegrees(float fov_degrees) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    constexpr float kMinFov = 1.0F;
    constexpr float kMaxFov = 179.0F;
    const float clamped = std::clamp(fov_degrees, kMinFov, kMaxFov);
    cam_ref->get().SetFieldOfView(glm::radians(clamped));
    service_->PersistActiveCameraSettings();
  }
}

auto CameraVm::GetPerspectiveNearPlane() const -> float
{
  if (!service_) {
    return 0.0F;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return 0.0F;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    return cam_ref->get().GetNearPlane();
  }
  return 0.0F;
}

auto CameraVm::GetPerspectiveFarPlane() const -> float
{
  if (!service_) {
    return 0.0F;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return 0.0F;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    return cam_ref->get().GetFarPlane();
  }
  return 0.0F;
}

auto CameraVm::SetPerspectiveNearPlane(float near_plane) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    constexpr float kMinNear = 0.001F;
    constexpr float kMinRange = 0.001F;
    const float clamped_near = std::max(near_plane, kMinNear);
    const float far_plane
      = std::max(cam_ref->get().GetFarPlane(), clamped_near + kMinRange);
    cam_ref->get().SetNearPlane(clamped_near);
    cam_ref->get().SetFarPlane(far_plane);
    service_->PersistActiveCameraSettings();
  }
}

auto CameraVm::SetPerspectiveFarPlane(float far_plane) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    constexpr float kMinRange = 0.001F;
    const float near_plane = cam_ref->get().GetNearPlane();
    const float clamped_far = std::max(far_plane, near_plane + kMinRange);
    cam_ref->get().SetFarPlane(clamped_far);
    service_->PersistActiveCameraSettings();
  }
}

auto CameraVm::GetOrthoWidth() const -> float
{
  if (!service_) {
    return 0.0F;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return 0.0F;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    const auto extents = cam_ref->get().GetExtents();
    return extents.at(1) - extents.at(0);
  }
  return 0.0F;
}

auto CameraVm::GetOrthoHeight() const -> float
{
  if (!service_) {
    return 0.0F;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return 0.0F;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    const auto extents = cam_ref->get().GetExtents();
    return extents.at(3) - extents.at(2);
  }
  return 0.0F;
}

auto CameraVm::GetOrthoNearPlane() const -> float
{
  if (!service_) {
    return 0.0F;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return 0.0F;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    return cam_ref->get().GetExtents().at(kOrthoNearIndex);
  }
  return 0.0F;
}

auto CameraVm::GetOrthoFarPlane() const -> float
{
  if (!service_) {
    return 0.0F;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return 0.0F;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    return cam_ref->get().GetExtents().at(kOrthoFarIndex);
  }
  return 0.0F;
}

auto CameraVm::SetOrthoWidth(float width) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    constexpr float kMinSize = 0.001F;
    auto extents = cam_ref->get().GetExtents();
    const float clamped = std::max(width, kMinSize);
    const float center = 0.5F * (extents.at(0) + extents.at(1));
    extents.at(0) = center - (0.5F * clamped);
    extents.at(1) = center + (0.5F * clamped);
    cam_ref->get().SetExtents(extents.at(0), extents.at(1), extents.at(2),
      extents.at(3), extents.at(kOrthoNearIndex), extents.at(kOrthoFarIndex));
    service_->PersistActiveCameraSettings();
  }
}

auto CameraVm::SetOrthoHeight(float height) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    constexpr float kMinSize = 0.001F;
    auto extents = cam_ref->get().GetExtents();
    const float clamped = std::max(kMinSize, height);
    // Resizing keeps the framing ratio, as an authored OrthographicSize does.
    const float ratio
      = (extents.at(1) - extents.at(0)) / (extents.at(3) - extents.at(2));
    const float center = 0.5F * (extents.at(2) + extents.at(3));
    extents.at(2) = center - (0.5F * clamped);
    extents.at(3) = center + (0.5F * clamped);
    const float center_x = 0.5F * (extents.at(0) + extents.at(1));
    extents.at(0) = center_x - (0.5F * clamped * ratio);
    extents.at(1) = center_x + (0.5F * clamped * ratio);
    cam_ref->get().SetExtents(extents.at(0), extents.at(1), extents.at(2),
      extents.at(3), extents.at(kOrthoNearIndex), extents.at(kOrthoFarIndex));
    service_->PersistActiveCameraSettings();
  }
}

auto CameraVm::SetOrthoNearPlane(float near_plane) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    constexpr float kMinNear = 0.001F;
    constexpr float kMinRange = 0.001F;
    auto extents = cam_ref->get().GetExtents();
    const float clamped_near = std::max(near_plane, kMinNear);
    const float far_plane
      = std::max(extents.at(kOrthoFarIndex), clamped_near + kMinRange);
    extents.at(kOrthoNearIndex) = clamped_near;
    extents.at(kOrthoFarIndex) = far_plane;
    cam_ref->get().SetExtents(extents.at(0), extents.at(1), extents.at(2),
      extents.at(3), extents.at(kOrthoNearIndex), extents.at(kOrthoFarIndex));
    service_->PersistActiveCameraSettings();
  }
}

auto CameraVm::SetOrthoFarPlane(float far_plane) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    constexpr float kMinRange = 0.001F;
    auto extents = cam_ref->get().GetExtents();
    extents.at(kOrthoFarIndex)
      = std::max(far_plane, extents.at(kOrthoNearIndex) + kMinRange);
    cam_ref->get().SetExtents(extents.at(0), extents.at(1), extents.at(2),
      extents.at(3), extents.at(kOrthoNearIndex), extents.at(kOrthoFarIndex));
    service_->PersistActiveCameraSettings();
  }
}

auto CameraVm::SetOrthographic(const bool orthographic) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive() || HasOrthographicCamera() == orthographic) {
    return;
  }

  // Match the visible height at the orbit target (or a nominal distance).
  constexpr float kNominalDistance = 10.0F;
  constexpr float kMinFovY = glm::radians(1.0F);
  constexpr float kMaxFovY = glm::radians(179.0F);
  auto distance = kNominalDistance;
  if (camera_rig_) {
    if (const auto orbit = camera_rig_->GetOrbitController(); orbit) {
      constexpr float kMinDistance = 0.01F;
      distance = std::max(orbit->GetDistance(), kMinDistance);
    }
  }

  if (orthographic) {
    auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>();
    if (!cam_ref) {
      return;
    }
    const auto& source = cam_ref->get();
    const float height
      = 2.0F * distance * std::tan(0.5F * source.GetFieldOfView());
    const float half_w = 0.5F * height * source.GetAspectRatio();
    auto replacement = std::make_unique<scene::OrthographicCamera>();
    replacement->SetExtents(-half_w, half_w, -0.5F * height, 0.5F * height,
      source.GetNearPlane(), source.GetFarPlane());
    replacement->SetAspectMode(source.GetAspectMode());
    replacement->SetExposure(source.Exposure());
    const float far_plane = source.GetFarPlane();
    std::ignore = camera.ReplaceCamera(std::move(replacement));

    // The parallel image does not depend on distance, but the near plane
    // does: back away so the scene around the camera is not clipped.
    auto transform = camera.GetTransform();
    const auto rotation = transform.GetLocalRotation().value_or(
      glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
    const auto position
      = transform.GetLocalPosition().value_or(glm::vec3(0.0F));
    std::ignore = transform.SetLocalPosition(
      position - (rotation * space::look::Forward) * (0.5F * far_plane));
    if (camera_rig_) {
      camera_rig_->SyncFromActiveCamera();
    }
  } else {
    auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>();
    if (!cam_ref) {
      return;
    }
    const auto& source = cam_ref->get();
    const auto ext = source.GetExtents();
    const float height = ext.at(3) - ext.at(2);
    auto replacement = std::make_unique<scene::PerspectiveCamera>();
    replacement->SetFieldOfView(std::clamp(
      2.0F * std::atan(0.5F * height / distance), kMinFovY, kMaxFovY));
    replacement->SetAspectRatio((ext.at(1) - ext.at(0)) / height);
    replacement->SetAspectMode(source.GetAspectMode());
    replacement->SetNearPlane(ext.at(kOrthoNearIndex));
    replacement->SetFarPlane(ext.at(kOrthoFarIndex));
    replacement->SetExposure(source.Exposure());
    std::ignore = camera.ReplaceCamera(std::move(replacement));
  }
  service_->PersistActiveCameraSettings();
}

auto CameraVm::GetAspectMode() const -> CameraAspectMode
{
  if (!service_) {
    return CameraAspectMode::kAuto;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return CameraAspectMode::kAuto;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    return cam_ref->get().GetAspectMode();
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    return cam_ref->get().GetAspectMode();
  }
  return CameraAspectMode::kAuto;
}

auto CameraVm::SetAspectMode(const CameraAspectMode mode) -> void
{
  if (!service_) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    cam_ref->get().SetAspectMode(mode);
  } else if (auto ortho = camera.GetCameraAs<scene::OrthographicCamera>();
    ortho) {
    ortho->get().SetAspectMode(mode);
  } else {
    return;
  }
  service_->PersistActiveCameraSettings();
}

auto CameraVm::GetAspectRatio() const -> float
{
  if (!service_) {
    return kDefaultCameraAspectRatio;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return kDefaultCameraAspectRatio;
  }
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    return cam_ref->get().GetAspectRatio();
  }
  if (auto cam_ref = camera.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    const auto ext = cam_ref->get().GetExtents();
    return (ext.at(1) - ext.at(0)) / (ext.at(3) - ext.at(2));
  }
  return kDefaultCameraAspectRatio;
}

auto CameraVm::SetAspectRatio(const float ratio) -> void
{
  if (!service_ || !std::isfinite(ratio)) {
    return;
  }
  auto& camera = service_->GetActiveCamera();
  if (!camera.IsAlive()) {
    return;
  }
  constexpr float kMinRatio = 0.1F;
  constexpr float kMaxRatio = 10.0F;
  const float clamped = std::clamp(ratio, kMinRatio, kMaxRatio);
  if (auto cam_ref = camera.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    cam_ref->get().SetAspectRatio(clamped);
  } else if (auto ortho = camera.GetCameraAs<scene::OrthographicCamera>();
    ortho) {
    auto ext = ortho->get().GetExtents();
    const float center_x = 0.5F * (ext.at(0) + ext.at(1));
    const float half_w = 0.5F * (ext.at(3) - ext.at(2)) * clamped;
    ortho->get().SetExtents(center_x - half_w, center_x + half_w, ext.at(2),
      ext.at(3), ext.at(kOrthoNearIndex), ext.at(kOrthoFarIndex));
  } else {
    return;
  }
  service_->PersistActiveCameraSettings();
}

auto CameraVm::GetDronePathPoints() const -> std::span<const glm::vec3>
{
  if (camera_rig_) {
    if (const auto drone = camera_rig_->GetDroneController()) {
      return { drone->GetPathPoints() };
    }
  }
  static const std::vector<glm::vec3> empty;
  return { empty };
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
  if (service_) {
    service_->RequestReset();
  }
}

auto CameraVm::PersistActiveCameraSettings() -> void
{
  if (service_) {
    service_->PersistActiveCameraSettings();
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
        service_->GetDroneFocusOffsetY(), service_->GetDroneFocusHeight()));
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
