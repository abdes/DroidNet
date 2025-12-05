//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>

#include "ViewRenderer.h"
#include <Oxygen/Renderer/Renderer.h>

namespace oxygen::examples::multiview {
auto ViewRenderer::Configure(const Config& config) -> void
{
  config_ = config;

  // Create depth pass config and pass (persistent)
  if (!depth_pass_config_) {
    depth_pass_config_ = std::make_shared<engine::DepthPrePassConfig>();
    depth_pass_config_->debug_name
      = config.wireframe ? "WireframeDepthPrePass" : "DepthPrePass";
  }
  depth_pass_config_->depth_texture = config.depth_texture;

  if (!depth_pass_) {
    depth_pass_ = std::make_shared<engine::DepthPrePass>(depth_pass_config_);
  }

  // Create shader pass config and pass (persistent)
  if (!shader_pass_config_) {
    shader_pass_config_ = std::make_shared<engine::ShaderPassConfig>();
    shader_pass_config_->debug_name
      = config.wireframe ? "WireframeShaderPass" : "ShaderPass";
  }
  shader_pass_config_->color_texture = config.color_texture;
  shader_pass_config_->clear_color = config.clear_color;
  shader_pass_config_->fill_mode = config.wireframe
    ? graphics::FillMode::kWireFrame
    : graphics::FillMode::kSolid;

  if (!shader_pass_) {
    shader_pass_ = std::make_shared<engine::ShaderPass>(shader_pass_config_);
  }

  LOG_F(INFO,
    "[ViewRenderer] Configured: color_tex={}, depth_tex={}, wireframe={}, "
    "clear_color=({},{},{},{})",
    static_cast<const void*>(config.color_texture.get()),
    static_cast<const void*>(config.depth_texture.get()), config.wireframe,
    config.clear_color.r, config.clear_color.g, config.clear_color.b,
    config.clear_color.a);
}

auto ViewRenderer::RegisterWithEngine(engine::Renderer& engine_renderer,
  ViewId view_id, engine::ViewResolver resolver) -> void
{
  // Store bookkeeping and register render graph factory forwarding to our
  // Render method.
  registered_engine_renderer_ = &engine_renderer;
  registered_view_id_ = view_id;

  LOG_F(INFO, "[ViewRenderer] RegisterWithEngine: view_id={}, renderer_ptr={}",
    view_id.get(), static_cast<const void*>(&engine_renderer));

  engine_renderer.RegisterView(view_id, std::move(resolver),
    [this](ViewId id, const engine::RenderContext& rc,
      graphics::CommandRecorder& rec) -> co::Co<> {
      // Forward to the per-view renderer's Render implementation.
      co_await this->Render(rc, rec);
      co_return;
    });
}

auto ViewRenderer::UnregisterFromEngine() -> void
{
  if (registered_engine_renderer_ && registered_view_id_.get() != 0) {
    LOG_F(INFO,
      "[ViewRenderer] UnregisterFromEngine: view_id={}, renderer_ptr={}",
      registered_view_id_.get(),
      static_cast<const void*>(registered_engine_renderer_));
    registered_engine_renderer_->UnregisterView(registered_view_id_);
  }
  registered_engine_renderer_ = nullptr;
  registered_view_id_ = ViewId {};
}

auto ViewRenderer::ResetConfiguration() -> void
{
  // Clear the external configuration and ensure any persistent pass configs
  // do not retain stale texture references.
  config_.reset();

  if (depth_pass_config_) {
    depth_pass_config_->depth_texture.reset();
  }

  if (shader_pass_config_) {
    shader_pass_config_->color_texture.reset();
    shader_pass_config_->clear_color.reset();
    shader_pass_config_->fill_mode = graphics::FillMode::kSolid;
  }
  LOG_F(INFO, "[ViewRenderer] Configuration reset (cleared textures)");
}

auto ViewRenderer::Render(const engine::RenderContext& ctx,
  graphics::CommandRecorder& recorder) const -> co::Co<>
{
  if (!config_.has_value()) {
    LOG_F(ERROR, "[ViewRenderer] Cannot render - not configured!");
    co_return;
  }

  // Check if RenderContext has prepared scene data
  const auto psf = ctx.current_view.prepared_frame;
  LOG_F(INFO,
    "[ViewRenderer] Starting render: prepared_frame={}, valid={}, "
    "draw_metadata_size={}",
    static_cast<const void*>(psf.get()),
    psf ? (psf->IsValid() ? "true" : "false") : "null",
    psf ? psf->draw_metadata_bytes.size() : 0);

  // Depth pre-pass
  co_await RenderDepthPrePass(ctx, recorder);

  // Color pass
  co_await RenderColorPass(ctx, recorder);

  LOG_F(INFO, "[ViewRenderer] Render complete");
  co_return;
}

auto ViewRenderer::RenderDepthPrePass(const engine::RenderContext& ctx,
  graphics::CommandRecorder& recorder) const -> co::Co<>
{
  LOG_SCOPE_F(INFO, "[ViewRenderer] DepthPrePass");

  if (!depth_pass_) {
    LOG_F(ERROR, "[ViewRenderer] DepthPrePass not configured!");
    co_return;
  }

  // Prepare resources (transitions)
  co_await depth_pass_->PrepareResources(ctx, recorder);

  // Execute depth writes
  co_await depth_pass_->Execute(ctx, recorder);

  co_return;
}

auto ViewRenderer::RenderColorPass(const engine::RenderContext& ctx,
  graphics::CommandRecorder& recorder) const -> co::Co<>
{
  LOG_SCOPE_F(INFO, "[ViewRenderer] ColorPass");

  if (!shader_pass_) {
    LOG_F(ERROR, "[ViewRenderer] ShaderPass not configured!");
    co_return;
  }

  // Prepare resources (transitions)
  co_await shader_pass_->PrepareResources(ctx, recorder);

  // Execute draw calls
  co_await shader_pass_->Execute(ctx, recorder);

  co_return;
}

} // namespace oxygen::examples::multiview
