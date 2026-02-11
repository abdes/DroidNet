//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/UI/LightCullingVm.h"

namespace oxygen::examples::ui {

LightCullingVm::LightCullingVm(
  observer_ptr<LightCullingSettingsService> service,
  std::function<void()> on_cluster_mode_changed)
  : service_(service)
  , on_cluster_mode_changed_(std::move(on_cluster_mode_changed))
{
  Refresh();
}

auto LightCullingVm::GetVisualizationMode() -> ShaderDebugMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return visualization_mode_;
}

auto LightCullingVm::GetDepthSlices() -> int
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return depth_slices_;
}

auto LightCullingVm::GetUseCameraZ() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return use_camera_z_;
}

auto LightCullingVm::GetZNear() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return z_near_;
}

auto LightCullingVm::GetZFar() -> float
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return z_far_;
}

auto LightCullingVm::SetVisualizationMode(ShaderDebugMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (visualization_mode_ == mode) {
    return;
  }

  visualization_mode_ = mode;
  service_->SetVisualizationMode(mode);
  epoch_ = service_->GetEpoch();
}

auto LightCullingVm::SetDepthSlices(int slices) -> void
{
  std::lock_guard lock(mutex_);
  if (depth_slices_ == slices) {
    return;
  }

  depth_slices_ = slices;
  service_->SetDepthSlices(slices);
  epoch_ = service_->GetEpoch();

  NotifyClusterModeChanged();
}

auto LightCullingVm::SetUseCameraZ(bool use_camera) -> void
{
  std::lock_guard lock(mutex_);
  if (use_camera_z_ == use_camera) {
    return;
  }

  use_camera_z_ = use_camera;
  service_->SetUseCameraZ(use_camera);
  epoch_ = service_->GetEpoch();

  NotifyClusterModeChanged();
}

auto LightCullingVm::SetZNear(float z_near) -> void
{
  std::lock_guard lock(mutex_);
  if (z_near_ == z_near) {
    return;
  }

  z_near_ = z_near;
  service_->SetZNear(z_near);
  epoch_ = service_->GetEpoch();

  NotifyClusterModeChanged();
}

auto LightCullingVm::SetZFar(float z_far) -> void
{
  std::lock_guard lock(mutex_);
  if (z_far_ == z_far) {
    return;
  }

  z_far_ = z_far;
  service_->SetZFar(z_far);
  epoch_ = service_->GetEpoch();

  NotifyClusterModeChanged();
}

auto LightCullingVm::Refresh() -> void
{
  visualization_mode_ = service_->GetVisualizationMode();
  depth_slices_ = service_->GetDepthSlices();
  use_camera_z_ = service_->GetUseCameraZ();
  z_near_ = service_->GetZNear();
  z_far_ = service_->GetZFar();
  epoch_ = service_->GetEpoch();
}

auto LightCullingVm::IsStale() const -> bool
{
  return epoch_ != service_->GetEpoch();
}

auto LightCullingVm::NotifyClusterModeChanged() -> void
{
  if (on_cluster_mode_changed_) {
    on_cluster_mode_changed_();
  }
}

} // namespace oxygen::examples::ui
