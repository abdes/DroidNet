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
#include <Oxygen/Vortex/CompositionView.h>

#include "DemoShell/Runtime/RendererUiTypes.h"
#include "DemoShell/Services/DomainService.h"

namespace oxygen::renderer {
class RenderingPipeline;
} // namespace oxygen::renderer

namespace oxygen::vortex {
class Renderer;
} // namespace oxygen::vortex

namespace oxygen::examples {
class SettingsService;

class LightCullingSettingsService : public DomainService {
public:
  LightCullingSettingsService() = default;
  ~LightCullingSettingsService() override = default;

  OXYGEN_MAKE_NON_COPYABLE(LightCullingSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(LightCullingSettingsService)

  virtual auto Initialize(observer_ptr<renderer::RenderingPipeline> pipeline)
    -> void;
  virtual auto BindVortexRenderer(observer_ptr<vortex::Renderer> renderer)
    -> void;

  [[nodiscard]] virtual auto GetVisualizationMode() const
    -> engine::ShaderDebugMode;
  virtual auto SetVisualizationMode(engine::ShaderDebugMode mode) -> void;

  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t override;

  auto OnFrameStart(const engine::FrameContext& context) -> void override;
  auto OnSceneActivated(scene::Scene& scene) -> void override;
  auto OnMainViewReady(const engine::FrameContext& context,
    const vortex::CompositionView& view) -> void override;

private:
  auto ApplyVisualizationSettings() -> void;

  static constexpr auto kVisualizationModeKey
    = "light_culling.visualization_mode";

  observer_ptr<renderer::RenderingPipeline> pipeline_ { nullptr };
  observer_ptr<vortex::Renderer> vortex_renderer_ { nullptr };
  mutable std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples
