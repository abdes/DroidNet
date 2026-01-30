//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>

#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/UI/LightCullingVm.h"

namespace oxygen::examples::ui {

LightCullingVm::LightCullingVm(observer_ptr<LightCullingSettingsService> service,
  observer_ptr<engine::ShaderPassConfig> shader_config,
  observer_ptr<engine::LightCullingPassConfig> culling_config,
  std::function<void()> on_cluster_mode_changed)
  : service_(service)
  , shader_config_(shader_config)
  , culling_config_(culling_config)
  , on_cluster_mode_changed_(std::move(on_cluster_mode_changed))
{
  Refresh();
  // Apply loaded settings to pass configs
  ApplyToPassConfigs();
}

auto LightCullingVm::GetVisualizationMode() -> ShaderDebugMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return visualization_mode_;
}

auto LightCullingVm::IsClusteredCulling() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return use_clustered_culling_;
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

  // Apply directly to shader config
  if (shader_config_) {
    shader_config_->debug_mode = mode;
  }
}

auto LightCullingVm::SetClusteredCulling(bool enabled) -> void
{
  std::lock_guard lock(mutex_);
  if (use_clustered_culling_ == enabled) {
    return;
  }

  use_clustered_culling_ = enabled;
  service_->SetUseClusteredCulling(enabled);
  epoch_ = service_->GetEpoch();

  ApplyToPassConfigs();
  NotifyClusterModeChanged();
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

  ApplyToPassConfigs();
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

  ApplyToPassConfigs();
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

  ApplyToPassConfigs();
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

  ApplyToPassConfigs();
  NotifyClusterModeChanged();
}

auto LightCullingVm::SetPassConfigs(
  observer_ptr<engine::ShaderPassConfig> shader_config,
  observer_ptr<engine::LightCullingPassConfig> culling_config) -> void
{
  std::lock_guard lock(mutex_);
  shader_config_ = shader_config;
  culling_config_ = culling_config;
  ApplyToPassConfigs();
}

auto LightCullingVm::ApplyToPassConfigs() -> void
{
  // Assume mutex is held if called from setters, but this might be called
  // from SetPassConfigs or Ctor. To be safe, we could use a recursive mutex
  // or a private internal method. For now, since we only use plain mutex,
  // we assume the top-level methods lock it.

  // NOTE: visualization_mode_ (ShaderDebugMode) is now applied every frame
  // by the application module to avoid conflicts between Rendering and
  // Lighting ViewModels.

  if (!culling_config_) {
    return;
  }

  auto& cluster = culling_config_->cluster;

  // Tile size is fixed at 16 (compile-time constant in compute shader)
  cluster.tile_size_px = 16;

  if (use_clustered_culling_) {
    cluster.depth_slices
      = (depth_slices_ >= 2) ? static_cast<uint32_t>(depth_slices_) : 24;
  } else {
    cluster.depth_slices = 1; // Tile-based mode
  }

  // Apply Z range - 0 means "use camera near/far"
  if (use_camera_z_) {
    cluster.z_near = 0.0F;
    cluster.z_far = 0.0F;
  } else {
    cluster.z_near = z_near_;
    cluster.z_far = z_far_;
  }
}

auto LightCullingVm::Refresh() -> void
{
  visualization_mode_ = service_->GetVisualizationMode();
  use_clustered_culling_ = service_->GetUseClusteredCulling();
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
