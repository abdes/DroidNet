//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Renderer/Passes/RenderPass.h>
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

//! Configuration for a shading pass (main geometry + lighting).
struct ShaderPassConfig {
  //! Optional explicit color texture to render into (overrides framebuffer if
  //! set).
  std::shared_ptr<const graphics::Texture> color_texture = nullptr;

  //! Optional clear color for the color attachment. If present, will override
  //! the default clear value in the texture's descriptor.
  std::optional<graphics::Color> clear_color {};

  //! Debug name for diagnostics.
  std::string debug_name { "ShaderPass" };
};

//! Shading pass: draws geometry and applies lighting in a Forward+ or forward
//! pipeline.
class ShaderPass : public RenderPass {
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
};

} // namespace oxygen::engine
