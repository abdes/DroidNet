//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::graphics {
class Framebuffer;
}

#include <Oxygen/OxCo/Co.h>

namespace oxygen::examples::common {

struct AsyncEngineApp;

//! Small component that owns a lightweight render-graph and per-frame
//! RenderContext used by example modules.
/*!
  The component encapsulates the common example pattern of creating a small
  set of passes (DepthPrePass, ShaderPass, TransparentPass) and exposes the
  pass objects + their configuration objects so example modules can access
  and tweak them. It also holds an engine::RenderContext instance which is
  reused across frames by examples.
*/
class RenderGraph final : public oxygen::Component {
  OXYGEN_COMPONENT(RenderGraph)

public:
  explicit RenderGraph(const AsyncEngineApp& app) noexcept;
  ~RenderGraph() noexcept override = default;

  // Create default pass objects and configs if missing.
  auto SetupRenderPasses() -> void;

  // Accessors
  auto GetRenderContext() noexcept -> oxygen::engine::RenderContext&
  {
    return render_context_;
  }

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

  auto GetWireframeShaderPassConfig()
    -> std::shared_ptr<oxygen::engine::ShaderPassConfig>&
  {
    return wireframe_shader_pass_config_;
  }

  auto GetWireframeDepthPassConfig()
    -> std::shared_ptr<oxygen::engine::DepthPrePassConfig>&
  {
    return wireframe_depth_pass_config_;
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
    observer_ptr<const oxygen::graphics::Framebuffer> fb) -> void;

  auto PrepareForWireframeRenderFrame(
    observer_ptr<const oxygen::graphics::Framebuffer> fb) -> void;

  // Execute the configured pass list (DepthPrePass, ShaderPass,
  // TransparentPass) using the supplied recorder. This reuses the
  // RenderGraph's internal RenderContext and performs the PrepareResources
  // -> Execute sequence for each pass. Implemented as a coroutine to match
  // the renderer's usage pattern.
  auto RunPasses(const oxygen::engine::RenderContext& ctx,
    oxygen::graphics::CommandRecorder& recorder) -> co::Co<>;

  auto RunWireframePasses(const oxygen::engine::RenderContext& ctx,
    oxygen::graphics::CommandRecorder& recorder) -> co::Co<>;

private:
  // Passes and configuration owned by the component
  std::shared_ptr<oxygen::engine::DepthPrePass> depth_pass_ {};
  std::shared_ptr<oxygen::engine::DepthPrePassConfig> depth_pass_config_ {};

  std::shared_ptr<oxygen::engine::ShaderPass> shader_pass_ {};
  std::shared_ptr<oxygen::engine::ShaderPassConfig> shader_pass_config_ {};

  std::shared_ptr<oxygen::engine::DepthPrePass> wireframe_depth_pass_ {};
  std::shared_ptr<oxygen::engine::DepthPrePassConfig>
    wireframe_depth_pass_config_ {};

  std::shared_ptr<oxygen::engine::ShaderPass> wireframe_shader_pass_ {};
  std::shared_ptr<oxygen::engine::ShaderPassConfig>
    wireframe_shader_pass_config_ {};

  std::shared_ptr<oxygen::engine::TransparentPass> transparent_pass_ {};
  std::shared_ptr<oxygen::engine::TransparentPass::Config>
    transparent_pass_config_ {};

  // Shared per-frame render context used by example modules.
  oxygen::engine::RenderContext render_context_ {};
};

} // namespace oxygen::examples::common
