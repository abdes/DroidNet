//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/AutoExposurePass.h>
#include <Oxygen/Renderer/Passes/GroundGridPass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>

#include "DemoShell/Runtime/CompositionView.h"
#include "DemoShell/Runtime/PipelineFeature.h"
#include "DemoShell/Runtime/RenderMode.h"

namespace oxygen {
namespace engine {
  class FrameContext;
  class Renderer;
  struct ShaderPassConfig;
  struct TransparentPassConfig;
  struct LightCullingPassConfig;
  struct CompositionSubmission;
}
namespace scene {
  class Scene;
}
namespace graphics {
  class Framebuffer;
}
} // namespace oxygen

namespace oxygen::examples {

class RenderingPipeline : public Object {
public:
  RenderingPipeline() = default;
  ~RenderingPipeline() override = default;

  OXYGEN_MAKE_NON_COPYABLE(RenderingPipeline)
  OXYGEN_MAKE_NON_MOVABLE(RenderingPipeline)

  // Discovery
  [[nodiscard]] virtual auto GetSupportedFeatures() const -> PipelineFeature
    = 0;

  // === Granular Configuration (User-Facing) ===-----------------------------

  //! Sets the debug visualization mode for shading.
  virtual auto SetShaderDebugMode(engine::ShaderDebugMode /*mode*/) -> void { }

  //! Sets the render mode (solid/wireframe/overlay).
  virtual auto SetRenderMode(RenderMode /*mode*/) -> void { }

  //! Enables or disables GPU debug passes attached to the pipeline.
  virtual auto SetGpuDebugPassEnabled(bool /*enabled*/) -> void { }

  //! Enables/disables blue-noise jitter in atmosphere LUT generation.
  virtual auto SetAtmosphereBlueNoiseEnabled(bool /*enabled*/) -> void { }

  //! Sets the wireframe color used by dedicated wireframe passes.
  virtual auto SetWireframeColor(const graphics::Color& /*color*/) -> void { }

  //! Sets the debug visualization mode for light culling.
  virtual auto SetLightCullingVisualizationMode(
    engine::ShaderDebugMode /*mode*/) -> void
  {
  }

  //! Sets the number of depth slices for clustered culling.
  virtual auto SetClusterDepthSlices(uint32_t /*slices*/) -> void { }

  virtual auto SetExposureMode(engine::ExposureMode /*mode*/) -> void { }
  virtual auto SetExposureValue(float /*value*/) -> void { }
  virtual auto SetGamma(float /*gamma*/) -> void { }
  virtual auto SetToneMapper(engine::ToneMapper /*mode*/) -> void { }

  // Auto Exposure settings
  virtual auto SetAutoExposureAdaptationSpeedUp(float /*speed*/) -> void { }
  virtual auto SetAutoExposureAdaptationSpeedDown(float /*speed*/) -> void { }
  virtual auto SetAutoExposureLowPercentile(float /*percentile*/) -> void { }
  virtual auto SetAutoExposureHighPercentile(float /*percentile*/) -> void { }
  virtual auto SetAutoExposureMinLogLuminance(float /*luminance*/) -> void { }
  virtual auto SetAutoExposureLogLuminanceRange(float /*range*/) -> void { }
  virtual auto SetAutoExposureTargetLuminance(float /*luminance*/) -> void { }
  virtual auto SetAutoExposureSpotMeterRadius(float /*radius*/) -> void { }
  virtual auto SetAutoExposureMeteringMode(engine::MeteringMode /*mode*/)
    -> void
  {
  }

  //! Resets the auto-exposure history for all active views to the given initial
  //! EV value (referenced to ISO 100 / EV100).
  //!
  //! Useful when switching environments/presets to prevent adaptation artifacts
  //! (flashing).
  virtual auto ResetAutoExposure(float /*initial_ev*/) -> void { }

  //! Provide last mouse-down position for GPU debug overlays.
  virtual auto SetGpuDebugMouseDownPosition(
    std::optional<SubPixelPosition> /*position*/) -> void
  {
  }

  //! Updates the ground grid configuration (if supported by the pipeline).
  virtual auto SetGroundGridConfig(
    const engine::GroundGridPassConfig& /*config*/) -> void
  {
  }

  // === Advanced Configuration (Engine Debugging) ===------------------------

  virtual auto UpdateShaderPassConfig(
    const engine::ShaderPassConfig& /*config*/) -> void
  {
  }
  virtual auto UpdateTransparentPassConfig(
    const engine::TransparentPassConfig& /*config*/) -> void
  {
  }
  virtual auto UpdateLightCullingPassConfig(
    const engine::LightCullingPassConfig& /*config*/) -> void
  {
  }

  // [Phase: kFrameStart]
  //! Called at the beginning of each frame before any other phase.
  //! Use this to commit staged configuration changes or perform per-frame
  //! setup.
  virtual auto OnFrameStart(observer_ptr<engine::FrameContext> /*context*/,
    engine::Renderer& /*renderer*/) -> void
  {
  }

  // [Phase: kSceneMutation]
  // Register active views for the frame.
  // The pipeline iterates 'view_descs' and registers them with the renderer.
  virtual auto OnSceneMutation(observer_ptr<engine::FrameContext> frame_ctx,
    engine::Renderer& renderer, scene::Scene& scene,
    std::span<const CompositionView> view_descs,
    graphics::Framebuffer* composite_target) -> co::Co<>
    = 0;

  // [Phase: kPreRender]
  // Configure render passes and graph parameters.
  // Note: views are now identified by their ID or the descriptors from the
  // mutation phase.
  virtual auto OnPreRender(observer_ptr<engine::FrameContext> frame_ctx,
    engine::Renderer& renderer, std::span<const CompositionView> view_descs)
    -> co::Co<>
    = 0;

  // [Phase: kCompositing]
  // Submit final composition/post-process tasks.
  virtual auto OnCompositing(observer_ptr<engine::FrameContext> frame_ctx,
    engine::Renderer& renderer, graphics::Framebuffer* composite_target)
    -> co::Co<engine::CompositionSubmission>
    = 0;

  // Clear references to swapchain-backed textures before resize.
  virtual auto ClearBackbufferReferences() -> void { }
};

} // namespace oxygen::examples
