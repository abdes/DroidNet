//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include "DemoShell/Runtime/CompositionView.h"
#include "DemoShell/Runtime/RenderingPipeline.h"

namespace oxygen {
class AsyncEngine;
} // namespace oxygen

namespace oxygen::examples {

//! Implements a standard forward rendering pipeline.
/*!
  Manages the configuration and execution of a forward rendering pass sequence
  (Light Culling -> Z-Prepass -> Opaque -> Transparent) for multiple layers.
*/
class ForwardPipeline : public RenderingPipeline {
  OXYGEN_TYPED(ForwardPipeline)
public:
  explicit ForwardPipeline(observer_ptr<AsyncEngine> engine) noexcept;

  ~ForwardPipeline() override;

  // Pipeline Interface
  auto OnFrameStart(engine::FrameContext& context, engine::Renderer& renderer)
    -> void override;
  auto OnSceneMutation(engine::FrameContext& context,
    engine::Renderer& renderer, scene::Scene& scene,
    std::span<const CompositionView> view_descs,
    graphics::Framebuffer* target_framebuffer) -> co::Co<> override;

  auto OnPreRender(engine::FrameContext& context, engine::Renderer& renderer,
    std::span<const CompositionView> view_descs) -> co::Co<> override;

  auto OnCompositing(engine::FrameContext& context, engine::Renderer& renderer,
    graphics::Framebuffer* target_framebuffer)
    -> co::Co<engine::CompositionSubmission> override;

  auto ClearBackbufferReferences() -> void override;

  // Configuration
  [[nodiscard]] auto GetSupportedFeatures() const -> PipelineFeature override;

  auto SetShaderDebugMode(engine::ShaderDebugMode mode) -> void override;
  auto SetRenderMode(RenderMode mode) -> void override;
  auto SetWireframeColor(const graphics::Color& color) -> void override;
  auto SetLightCullingVisualizationMode(engine::ShaderDebugMode mode)
    -> void override;
  auto SetClusteredCullingEnabled(bool enabled) -> void override;
  auto SetClusterDepthSlices(uint32_t slices) -> void override;
  auto SetExposureMode(engine::ExposureMode mode) -> void override;
  auto SetExposureValue(float value) -> void override;
  auto SetToneMapper(engine::ToneMapper mode) -> void override;

  auto UpdateShaderPassConfig(const engine::ShaderPassConfig& config)
    -> void override;
  auto UpdateTransparentPassConfig(const engine::TransparentPassConfig& config)
    -> void override;
  auto UpdateLightCullingPassConfig(
    const engine::LightCullingPassConfig& config) -> void override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::examples
