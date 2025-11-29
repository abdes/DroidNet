//===----------------------------------------------------------------------===//
// RenderGraph copied from Examples/Common and adapted to the Editor module.
// Provides DepthPrePass, ShaderPass, TransparentPass, and a reusable
// per-frame RenderContext plus helpers for configuring per-frame
// attachments and running the pass sequence.
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::graphics { class Framebuffer; }

namespace Oxygen::Editor::EngineInterface {

class RenderGraph final {
public:
  RenderGraph() noexcept = default;
  ~RenderGraph() noexcept = default;

  // Create default pass objects and configs if missing.
  auto SetupRenderPasses() -> void;

  // Accessors
  auto& GetRenderContext() noexcept { return render_context_; }

  auto GetDepthPass() -> std::shared_ptr<oxygen::engine::DepthPrePass>&
  {
    return depth_pass_;
  }
  auto GetDepthPassConfig()
    -> std::shared_ptr<oxygen::engine::DepthPrePassConfig>&
  {
    return depth_pass_config_;
  }

  auto GetShaderPass() -> std::shared_ptr<oxygen::engine::ShaderPass>&
  {
    return shader_pass_;
  }
  auto GetShaderPassConfig()
    -> std::shared_ptr<oxygen::engine::ShaderPassConfig>&
  {
    return shader_pass_config_;
  }

  auto GetTransparentPass() -> std::shared_ptr<oxygen::engine::TransparentPass>&
  {
    return transparent_pass_;
  }
  auto GetTransparentPassConfig()
    -> std::shared_ptr<oxygen::engine::TransparentPass::Config>&
  {
    return transparent_pass_config_;
  }

  // Helpers for per-frame attachment management. Examples frequently need to
  // assign the current swapchain framebuffer to the render-context and wire
  // the pass configs to the back-buffer textures. These convenience helpers
  // centralize that logic so examples only call a single API point.
  auto ClearBackbufferReferences() -> void;

  auto PrepareForRenderFrame(
    const std::shared_ptr<oxygen::graphics::Framebuffer>& fb) -> void;

  // Execute the configured pass list (DepthPrePass, ShaderPass,
  // TransparentPass) using the supplied recorder. Implemented as a coroutine
  // to match the renderer's usage pattern.
  auto RunPasses(const oxygen::engine::RenderContext& ctx,
    oxygen::graphics::CommandRecorder& recorder) -> oxygen::co::Co<>;

private:
  // Passes and configuration owned by the component
  std::shared_ptr<oxygen::engine::DepthPrePass> depth_pass_ {};
  std::shared_ptr<oxygen::engine::DepthPrePassConfig> depth_pass_config_ {};

  std::shared_ptr<oxygen::engine::ShaderPass> shader_pass_ {};
  std::shared_ptr<oxygen::engine::ShaderPassConfig> shader_pass_config_ {};

  std::shared_ptr<oxygen::engine::TransparentPass> transparent_pass_ {};
  std::shared_ptr<oxygen::engine::TransparentPass::Config>
    transparent_pass_config_ {};

  // Shared per-frame render context used by the editor module.
  oxygen::engine::RenderContext render_context_ {};
};

} // namespace Oxygen::Editor::EngineInterface
