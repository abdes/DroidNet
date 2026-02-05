//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/Runtime/CompositionView.h"

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

//! Bitmask of supported rendering features for pipeline discovery.
enum class PipelineFeature : std::uint32_t {
  kNone = 0,
  kOpaqueShading = 1 << 0,
  kTransparentShading = 1 << 1,
  kLightCulling = 1 << 2,
  kPostProcess = 1 << 3,
  kAll = 0xFFFFFFFF
};

//! Render mode selection for pipelines.
enum class RenderMode : uint8_t {
  kSolid,
  kWireframe,
  kOverlayWireframe,
};

[[nodiscard]] inline auto to_string(RenderMode mode) -> std::string_view
{
  switch (mode) {
  case RenderMode::kWireframe:
    return "wireframe";
  case RenderMode::kOverlayWireframe:
    return "overlay_wireframe";
  case RenderMode::kSolid:
  default:
    return "solid";
  }
}

inline auto operator|(PipelineFeature a, PipelineFeature b) -> PipelineFeature
{
  return static_cast<PipelineFeature>(
    static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline auto operator&(PipelineFeature a, PipelineFeature b) -> PipelineFeature
{
  return static_cast<PipelineFeature>(
    static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

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

  //! Sets the wireframe color used by dedicated wireframe passes.
  virtual auto SetWireframeColor(const graphics::Color& /*color*/) -> void { }

  //! Sets the debug visualization mode for light culling.
  virtual auto SetLightCullingVisualizationMode(
    engine::ShaderDebugMode /*mode*/) -> void
  {
  }

  //! Sets whether clustered culling is enabled (false = tile-based).
  virtual auto SetClusteredCullingEnabled(bool /*enabled*/) -> void { }

  //! Sets the number of depth slices for clustered culling.
  virtual auto SetClusterDepthSlices(uint32_t /*slices*/) -> void { }

  virtual auto SetExposureMode(engine::ExposureMode /*mode*/) -> void { }
  virtual auto SetExposureValue(float /*value*/) -> void { }
  virtual auto SetToneMapper(engine::ToneMapper /*mode*/) -> void { }

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
    graphics::Framebuffer* final_output) -> co::Co<>
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
    engine::Renderer& renderer, graphics::Framebuffer* final_output)
    -> co::Co<engine::CompositionSubmission>
    = 0;

  // Clear references to swapchain-backed textures before resize.
  virtual auto ClearBackbufferReferences() -> void { }
};

} // namespace oxygen::examples
