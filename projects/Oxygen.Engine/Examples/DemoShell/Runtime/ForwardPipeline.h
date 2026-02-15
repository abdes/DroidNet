//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Base/Macros.h>

#include "DemoShell/Runtime/CompositionView.h"
#include "DemoShell/Runtime/RenderingPipeline.h"

namespace oxygen {
class AsyncEngine;
} // namespace oxygen

namespace oxygen::examples {
namespace internal {
  class ForwardPipelineImpl;
} // namespace internal

//! Implements a standard forward rendering pipeline.
/*!
 Manages the configuration and execution of a forward rendering pass sequence
 (Light Culling -> Z-Prepass -> Opaque -> Transparent) for multiple layers.

### Execution Model

- **Per-view coroutine**: Views are registered with the renderer and execute a
  render coroutine that writes HDR, tonemaps into SDR, and emits the SDR result
  for compositing.
- **Settings staging**: Configuration setters stage values that are applied on
  the engine thread during `OnFrameStart`.
- **Wireframe handling**: Pure wireframe renders in the HDR path; overlay
  wireframe renders after tonemapping in SDR to preserve line color.

@note These hooks are expected to be called on the engine thread only.
@see RenderingPipeline, WireframePass, ToneMapPass
*/
class ForwardPipeline : public RenderingPipeline {
  OXYGEN_TYPED(ForwardPipeline)
public:
  explicit ForwardPipeline(observer_ptr<AsyncEngine> engine) noexcept;
  ~ForwardPipeline() override;

  OXYGEN_MAKE_NON_COPYABLE(ForwardPipeline)
  OXYGEN_MAKE_NON_MOVABLE(ForwardPipeline)

  // Pipeline Interface
  //! Apply staged settings before render graph execution.
  auto OnFrameStart(observer_ptr<engine::FrameContext> context,
    engine::Renderer& renderer) -> void override;
  //! Register views and bind per-view render coroutines.
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context,
    engine::Renderer& renderer, scene::Scene& scene,
    std::span<const CompositionView> view_descs,
    graphics::Framebuffer* composite_target) -> co::Co<> override;

  auto OnPreRender(observer_ptr<engine::FrameContext> context,
    engine::Renderer& renderer, std::span<const CompositionView> view_descs)
    -> co::Co<> override;

  auto OnCompositing(observer_ptr<engine::FrameContext> context,
    engine::Renderer& renderer, graphics::Framebuffer* composite_target)
    -> co::Co<engine::CompositionSubmission> override;

  auto ClearBackbufferReferences() -> void override;

  // Configuration
  [[nodiscard]] auto GetSupportedFeatures() const -> PipelineFeature override;

  //! Stage a shader debug mode update.
  auto SetShaderDebugMode(engine::ShaderDebugMode mode) -> void override;
  //! Stage a render mode update (solid, wireframe, overlay wireframe).
  auto SetRenderMode(RenderMode mode) -> void override;
  //! Stage whether GPU debug passes should run.
  auto SetGpuDebugPassEnabled(bool enabled) -> void override;
  //! Stage atmosphere blue-noise jitter enable.
  auto SetAtmosphereBlueNoiseEnabled(bool enabled) -> void override;
  //! Stage the last mouse-down position for GPU debug overlays.
  auto SetGpuDebugMouseDownPosition(std::optional<SubPixelPosition> position)
    -> void override;
  //! Stage a wireframe line color update.
  auto SetWireframeColor(const graphics::Color& color) -> void override;
  auto SetLightCullingVisualizationMode(engine::ShaderDebugMode mode)
    -> void override;
  auto SetClusterDepthSlices(uint32_t slices) -> void override;
  auto SetExposureMode(engine::ExposureMode mode) -> void override;
  auto SetExposureValue(float value) -> void override;
  auto SetGamma(float gamma) -> void override;

  auto SetAutoExposureAdaptationSpeedUp(float speed) -> void override;
  auto SetAutoExposureAdaptationSpeedDown(float speed) -> void override;
  auto SetAutoExposureLowPercentile(float percentile) -> void override;
  auto SetAutoExposureHighPercentile(float percentile) -> void override;
  auto SetAutoExposureMinLogLuminance(float luminance) -> void override;
  auto SetAutoExposureLogLuminanceRange(float range) -> void override;
  auto SetAutoExposureTargetLuminance(float luminance) -> void override;
  auto SetAutoExposureSpotMeterRadius(float radius) -> void override;
  auto SetAutoExposureMeteringMode(engine::MeteringMode mode) -> void override;
  auto ResetAutoExposure(float initial_ev) -> void override;

  auto SetToneMapper(engine::ToneMapper mode) -> void override;
  auto SetGroundGridConfig(const engine::GroundGridPassConfig& config)
    -> void override;

  auto UpdateShaderPassConfig(const engine::ShaderPassConfig& config)
    -> void override;
  auto UpdateTransparentPassConfig(const engine::TransparentPassConfig& config)
    -> void override;
  auto UpdateLightCullingPassConfig(
    const engine::LightCullingPassConfig& config) -> void override;

private:
  std::unique_ptr<internal::ForwardPipelineImpl> impl_;
};

} // namespace oxygen::examples
