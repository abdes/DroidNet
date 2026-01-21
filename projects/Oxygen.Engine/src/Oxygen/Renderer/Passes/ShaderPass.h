//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
class Buffer;
class CommandRecorder;
class Texture;
class GraphicsPipelineDesc;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct RenderContext;

//! Debug visualization mode for the shader pass.
//!
//! These modes correspond to boolean defines in ForwardMesh_PS.hlsl.
//! The shader is compiled with different defines to create specialized
//! visualization variants.
enum class ShaderDebugMode : int {
  kDisabled = 0, //!< Normal PBR rendering (default)

  // Light culling debug modes
  kLightCullingHeatMap = 1, //!< Heat map of lights per cluster
  kDepthSlice = 2, //!< Visualize depth slice (clustered mode)
  kClusterIndex = 3, //!< Visualize cluster index as checkerboard

  // IBL debug modes
  kIblSpecular = 4, //!< Visualize IBL specular (prefilter map sampling)
  kIblRawSky = 5, //!< Visualize raw sky cubemap sampling (no prefilter)
  kIblRawSkyViewDir = 6, //!< Visualize raw sky cubemap (view direction)

  // Material/UV debug modes
  kBaseColor = 7, //!< Visualize base color texture (albedo)
  kUv0 = 8, //!< Visualize UV0 coordinates
  kOpacity = 9, //!< Visualize base alpha/opacity
};

//! Configuration for a shading pass (main geometry + lighting).
struct ShaderPassConfig {
  //! Optional explicit color texture to render into (overrides framebuffer if
  //! set).
  std::shared_ptr<const graphics::Texture> color_texture = nullptr;

  //! Whether to clear the color attachment at the start of this pass.
  //!
  //! This can be disabled when a later pass (e.g. SkyPass) guarantees full
  //! background coverage for pixels not written by opaque geometry.
  bool clear_color_target = true;

  //! When enabled, ShaderPass will automatically skip the clear if a SkyPass
  //! is registered in the current RenderContext.
  //!
  //! This is a performance optimization to avoid an otherwise full render
  //! target clear when the sky will fill background pixels (typically where
  //! depth remains at the clear value).
  bool auto_skip_clear_when_sky_pass_present = true;

  //! Optional clear color for the color attachment. If present, will override
  //! the default clear value in the texture's descriptor.
  std::optional<graphics::Color> clear_color {};

  //! Debug name for diagnostics.
  std::string debug_name { "ShaderPass" };
  //! Rasterization fill mode for this pass.
  graphics::FillMode fill_mode { graphics::FillMode::kSolid };
  //! Debug visualization mode (see ShaderDebugMode).
  ShaderDebugMode debug_mode { ShaderDebugMode::kDisabled };
};

//! Shading pass: draws geometry and applies lighting in a Forward+ or forward
//! pipeline.
class ShaderPass : public GraphicsRenderPass {
public:
  //! Configuration for the depth pre-pass.
  using Config = ShaderPassConfig;

  OXGN_RNDR_API explicit ShaderPass(std::shared_ptr<ShaderPassConfig> config);

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  auto SetupRenderTargets(graphics::CommandRecorder& recorder) const -> void;

  //! Convenience method to get the target texture for this pass. Prefers the
  //! texture explicitly specified in the configuration, falling back to the
  //! color attachment of the framebuffer in the RenderContext if not set.
  [[nodiscard]] auto GetColorTexture() const -> const graphics::Texture&;

  //! Convenience method to get the framebuffer specified in the context.
  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;

  //! Convenience method to get the clear color for the pass.
  [[nodiscard]] auto GetClearColor() const -> const graphics::Color&;

  [[nodiscard]] auto HasDepth() const -> bool;

  virtual auto SetupViewPortAndScissors(
    graphics::CommandRecorder& command_recorder) const -> void;

  // Draw submission uses base IssueDrawCalls with predicate to restrict to
  // opaque/masked so transparent geometry renders only in TransparentPass.

  //! Configuration for the depth pre-pass.
  std::shared_ptr<Config> config_;

  //! Cached debug mode from last PSO build
  ShaderDebugMode last_built_debug_mode_ { ShaderDebugMode::kDisabled };

  //! Cached pipeline state descriptions for partition-aware execution.
  std::optional<graphics::GraphicsPipelineDesc> pso_opaque_single_ {};
  std::optional<graphics::GraphicsPipelineDesc> pso_opaque_double_ {};
  std::optional<graphics::GraphicsPipelineDesc> pso_masked_single_ {};
  std::optional<graphics::GraphicsPipelineDesc> pso_masked_double_ {};
};

} // namespace oxygen::engine
