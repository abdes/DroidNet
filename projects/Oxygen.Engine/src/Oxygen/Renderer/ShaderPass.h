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

  OXGN_RNDR_API auto PrepareResources(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<> override;

  OXGN_RNDR_API auto Execute(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<> override;

  auto IsEnabled() const -> bool override
  {
    return config_ && config_->enabled;
  }

private:
  //! Validates the current configuration.
  auto ValidateConfig() -> void;

  //! Creates the pipeline state description for this pass.
  virtual auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc;

  //! Determines if the pipeline state needs to be rebuilt.
  virtual auto NeedRebuildPipelineState() const -> bool;

  //! Convenience method to get the target texture for this pass. Prefers the
  //! texture explicitly specified in the configuration, falling back to the
  //! color attachment of the framebuffer in the RenderContext if not set.
  [[nodiscard]] auto GetColorTexture() const -> const graphics::Texture&;

  //! Convenience method to get the draw list specified in the context.
  [[nodiscard]] auto GetDrawList() const
    -> const std::vector<const RenderItem*>&;

  //! Convenience method to get the framebuffer specified in the context.
  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;

  //! Convenience method to get the clear color for the pass.
  [[nodiscard]] auto GetClearColor() const -> const graphics::Color&;

  virtual auto SetupViewPortAndScissors(
    graphics::CommandRecorder& command_recorder) const -> void;

  //! Current render context.
  const RenderContext* context_ { nullptr };

  //! Configuration for the depth pre-pass.
  std::shared_ptr<Config> config_;

  //! Current viewport for the pass.
  std::optional<graphics::ViewPort> viewport_ {};

  //! Current scissor rectangle for the pass.
  std::optional<graphics::Scissors> scissors_ {};

  // Track the last built pipeline state object (PSO) description and hash, so
  // we can properly manage their caching and retrieval.
  std::optional<graphics::GraphicsPipelineDesc> last_built_pso_desc_;
};

} // namespace oxygen::engine
