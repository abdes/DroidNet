//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/RenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
class Buffer;
class RenderController;
class CommandRecorder;
class Texture;
class GraphicsPipelineDesc;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct RenderContext;

//! Configuration for a shading pass (main geometry + lighting).
struct ShaderPassConfig {
  //! Optional per-draw constant buffer (e.g., world matrices).
  std::shared_ptr<const graphics::Buffer> per_draw_constants = nullptr;

  //! Optional explicit color texture to render into (overrides framebuffer if
  //! set).
  std::shared_ptr<const graphics::Texture> color_texture = nullptr;

  //! Whether this pass is enabled for the current frame.
  bool enabled = true;

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

  auto IsEnabled() const -> bool override
  {
    return config_ && config_->enabled;
  }

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

  //! Convenience method to get the draw list specified in the context.
  [[nodiscard]] auto GetDrawList() const
    -> std::span<const RenderItem> override;

  //! Convenience method to get the framebuffer specified in the context.
  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;

  //! Convenience method to get the clear color for the pass.
  [[nodiscard]] auto GetClearColor() const -> const graphics::Color&;

  virtual auto SetupViewPortAndScissors(
    graphics::CommandRecorder& command_recorder) const -> void;

  //! Configuration for the depth pre-pass.
  std::shared_ptr<Config> config_;
};

} // namespace oxygen::engine
