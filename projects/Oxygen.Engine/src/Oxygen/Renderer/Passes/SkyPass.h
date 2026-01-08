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
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
class CommandRecorder;
class Texture;
class GraphicsPipelineDesc;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct RenderContext;

//! Configuration for the sky rendering pass.
struct SkyPassConfig {
  //! Optional explicit color texture to render into.
  std::shared_ptr<const graphics::Texture> color_texture = nullptr;

  //! Debug name for diagnostics.
  std::string debug_name { "SkyPass" };
};

//! Sky rendering pass: draws the sky background behind scene geometry.
/*!
 The SkyPass renders the sky as a fullscreen triangle using depth-test
 LESS_EQUAL at z=1.0 (sky at far plane). It must execute after opaque geometry
 rendering (ShaderPass) but before transparent geometry (TransparentPass).

 Rendering priority is handled in the shader:
 1. SkyAtmosphere (procedural) - if enabled
 2. SkySphere cubemap - if enabled and source is kCubemap
 3. SkySphere solid color - if enabled and source is kSolidColor
 4. Black fallback

 @see GraphicsRenderPass, ShaderPass, TransparentPass
*/
class SkyPass : public GraphicsRenderPass {
public:
  //! Configuration type for this pass.
  using Config = SkyPassConfig;

  OXGN_RNDR_API explicit SkyPass(std::shared_ptr<SkyPassConfig> config);

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  //! Gets the target texture for this pass.
  [[nodiscard]] auto GetColorTexture() const -> const graphics::Texture&;

  //! Gets the framebuffer from the render context.
  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;

  //! Sets up the render targets for sky rendering.
  auto SetupRenderTargets(graphics::CommandRecorder& recorder) const -> void;

  //! Sets up viewport and scissors based on color texture dimensions.
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  std::shared_ptr<SkyPassConfig> config_;
};

} // namespace oxygen::engine
