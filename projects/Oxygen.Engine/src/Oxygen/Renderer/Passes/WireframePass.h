//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::engine {

//! Configuration for the dedicated wireframe pass.
struct WireframePassConfig {
  //! Optional explicit color texture to render into (overrides framebuffer if
  //! set).
  std::shared_ptr<const graphics::Texture> color_texture = nullptr;

  //! Whether to clear the color attachment at the start of this pass.
  bool clear_color_target = true;

  //! Optional clear color for the color attachment.
  std::optional<graphics::Color> clear_color {};

  //! Whether to clear the depth attachment at the start of this pass.
  bool clear_depth_target = true;

  //! Color used for wireframe lines.
  graphics::Color wire_color { 1.0F, 1.0F, 1.0F, 1.0F };

  //! Whether to apply exposure compensation in the wireframe shader.
  //!
  //! Disable this when the wireframe output is already in SDR space.
  bool apply_exposure_compensation { true };

  //! Debug name for diagnostics.
  std::string debug_name { "WireframePass" };

  //! Whether the wireframe pass writes depth.
  bool depth_write_enable = true;
};

//! Wireframe render pass (unlit, constant color).
/*!
 Emits constant-color lines using a small pass-constants CBV. The pass caches
 its CBV and updates it when the wire color changes.

@note Call `SetWireColor` on the engine thread before `PrepareResources`.
*/
class WireframePass : public GraphicsRenderPass {
public:
  //! Configuration for the wireframe pass.
  using Config = WireframePassConfig;

  OXGN_RNDR_API explicit WireframePass(std::shared_ptr<Config> config);
  ~WireframePass() override;

  //! Update the wireframe color and mark pass constants dirty.
  OXGN_RNDR_API auto SetWireColor(const graphics::Color& color) -> void;

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  auto SetupRenderTargets(graphics::CommandRecorder& recorder) const -> void;
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  [[nodiscard]] auto GetColorTexture() const -> const graphics::Texture&;
  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;
  [[nodiscard]] auto GetClearColor() const -> const graphics::Color&;
  [[nodiscard]] auto HasDepth() const -> bool;

  std::shared_ptr<Config> config_ {};
  std::shared_ptr<graphics::Buffer> pass_constants_buffer_ {};
  void* pass_constants_mapped_ptr_ { nullptr };
  graphics::NativeView pass_constants_cbv_ {};
  ShaderVisibleIndex pass_constants_index_ { kInvalidShaderVisibleIndex };
  bool pass_constants_dirty_ { true };

  std::optional<graphics::GraphicsPipelineDesc> pso_opaque_single_ {};
  std::optional<graphics::GraphicsPipelineDesc> pso_opaque_double_ {};
  std::optional<graphics::GraphicsPipelineDesc> pso_masked_single_ {};
  std::optional<graphics::GraphicsPipelineDesc> pso_masked_double_ {};
};

} // namespace oxygen::engine
