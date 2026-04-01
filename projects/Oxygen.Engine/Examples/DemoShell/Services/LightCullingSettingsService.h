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

//! Settings persistence for LightCulling debug visualization.
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

  static constexpr auto kVisualizationModeKey
    = "light_culling.visualization_mode";

  observer_ptr<renderer::RenderingPipeline> pipeline_;
  mutable std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples
