//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Types/ShaderDebugMode.h>

#include "DemoShell/Services/DomainService.h"

namespace oxygen::renderer {
struct CompositionView;
class RenderingPipeline;
} // namespace oxygen::renderer

namespace oxygen::examples {
class SettingsService;

//! Settings persistence for light culling panel options.
/*!
 Owns UI-facing settings for culling mode, cluster configuration, and
 visualization mode, delegating persistence to `SettingsService` and exposing
 an epoch for cache invalidation.

### Key Features

- **Passive state**: Reads and writes via SettingsService without caching.
- **Epoch tracking**: Increments on each effective change.
- **Testable**: Virtual getters and setters for overrides in tests.

@see SettingsService
*/
class LightCullingSettingsService : public DomainService {
public:
  LightCullingSettingsService() = default;
  virtual ~LightCullingSettingsService() = default;

  OXYGEN_MAKE_NON_COPYABLE(LightCullingSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(LightCullingSettingsService)

  //! Associates the service with a rendering pipeline and synchronizes
  //! initial state.
  virtual auto Initialize(observer_ptr<renderer::RenderingPipeline> pipeline)
    -> void;

  //! Returns the number of depth slices for clustered culling.
  [[nodiscard]] virtual auto GetDepthSlices() const -> int;

  //! Sets the number of depth slices.
  virtual auto SetDepthSlices(int slices) -> void;

  //! Returns whether camera near/far planes should be used.
  [[nodiscard]] virtual auto GetUseCameraZ() const -> bool;

  //! Sets whether to use camera near/far planes.
  virtual auto SetUseCameraZ(bool use_camera) -> void;

  //! Returns the custom Z near value.
  [[nodiscard]] virtual auto GetZNear() const -> float;

  //! Sets the custom Z near value.
  virtual auto SetZNear(float z_near) -> void;

  //! Returns the custom Z far value.
  [[nodiscard]] virtual auto GetZFar() const -> float;

  //! Sets the custom Z far value.
  virtual auto SetZFar(float z_far) -> void;

  //! Returns the visualization debug mode.
  [[nodiscard]] virtual auto GetVisualizationMode() const
    -> engine::ShaderDebugMode;

  //! Sets the visualization debug mode.
  virtual auto SetVisualizationMode(engine::ShaderDebugMode mode) -> void;

  //! Returns the current settings epoch.
  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t override;

  auto OnFrameStart(const engine::FrameContext& context) -> void override;
  auto OnSceneActivated(scene::Scene& scene) -> void override;
  auto OnMainViewReady(const engine::FrameContext& context,
    const renderer::CompositionView& view) -> void override;

private:
  auto ApplyPipelineSettings() -> void;

  static constexpr auto kDepthSlicesKey = "light_culling.depth_slices";
  static constexpr auto kUseCameraZKey = "light_culling.use_camera_z";
  static constexpr auto kZNearKey = "light_culling.z_near";
  static constexpr auto kZFarKey = "light_culling.z_far";
  static constexpr auto kVisualizationModeKey
    = "light_culling.visualization_mode";

  static constexpr int kDefaultDepthSlices = 24;
  static constexpr float kDefaultZNear = 0.1F;
  static constexpr float kDefaultZFar = 1000.0F;

  observer_ptr<renderer::RenderingPipeline> pipeline_;
  mutable std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples
