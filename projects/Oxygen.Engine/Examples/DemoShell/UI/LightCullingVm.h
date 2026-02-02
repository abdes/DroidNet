//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

namespace oxygen::engine {
struct LightCullingPassConfig;
} // namespace oxygen::engine

namespace oxygen::examples {
class LightCullingSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

// Re-export ShaderDebugMode for convenience in UI code
using ShaderDebugMode = engine::ShaderDebugMode;

//! View model for light culling panel state.
/*!
 Caches light culling settings retrieved from `LightCullingSettingsService`,
 invalidating the cache based on the service epoch and applying UI changes back
 to the service and pass configs.

### Key Features

- **Epoch-driven refresh**: Reacquires state when stale.
- **Immediate persistence**: Setters forward changes to the service.
- **Pass config sync**: Applies changes directly to shader and culling configs.
- **Cluster mode callback**: Notifies when cluster mode changes (triggers PSO
rebuild).
- **Thread-safe**: Protected by a mutex.

@see oxygen::examples::LightCullingSettingsService
*/
class LightCullingVm {
public:
  //! Creates a view model backed by the provided settings service.
  explicit LightCullingVm(observer_ptr<LightCullingSettingsService> service,
    std::function<void()> on_cluster_mode_changed);

  //! Returns the cached visualization mode.
  [[nodiscard]] auto GetVisualizationMode() -> ShaderDebugMode;

  //! Returns whether clustered culling is enabled.
  [[nodiscard]] auto IsClusteredCulling() -> bool;

  //! Returns the cached depth slices count.
  [[nodiscard]] auto GetDepthSlices() -> int;

  //! Returns whether camera Z planes are used.
  [[nodiscard]] auto GetUseCameraZ() -> bool;

  //! Returns the cached Z near value.
  [[nodiscard]] auto GetZNear() -> float;

  //! Returns the cached Z far value.
  [[nodiscard]] auto GetZFar() -> float;

  //! Sets visualization mode and forwards to service.
  auto SetVisualizationMode(ShaderDebugMode mode) -> void;

  //! Sets clustered culling and forwards to service.
  auto SetClusteredCulling(bool enabled) -> void;

  //! Sets depth slices and forwards to service.
  auto SetDepthSlices(int slices) -> void;

  //! Sets camera Z usage and forwards to service.
  auto SetUseCameraZ(bool use_camera) -> void;

  //! Sets Z near and forwards to service.
  auto SetZNear(float z_near) -> void;

  //! Sets Z far and forwards to service.
  auto SetZFar(float z_far) -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;
  auto NotifyClusterModeChanged() -> void;

  mutable std::mutex mutex_ {};
  observer_ptr<LightCullingSettingsService> service_;
  std::function<void()> on_cluster_mode_changed_;
  std::uint64_t epoch_ { 0 };

  // Cached state
  ShaderDebugMode visualization_mode_ { ShaderDebugMode::kDisabled };
  bool use_clustered_culling_ { false };
  int depth_slices_ { 24 };
  bool use_camera_z_ { true };
  float z_near_ { 0.1F };
  float z_far_ { 1000.0F };
};

} // namespace oxygen::examples::ui
