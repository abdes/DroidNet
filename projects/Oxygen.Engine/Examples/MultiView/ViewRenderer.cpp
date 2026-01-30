//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>

#include "MultiView/ViewRenderer.h"

namespace oxygen::examples::multiview {
auto ViewRenderer::Configure(const ViewRenderData& data) -> void
{
  CHECK_F(static_cast<bool>(data.color_texture),
    "ViewRenderData requires color_texture");
  CHECK_F(static_cast<bool>(data.depth_texture),
    "ViewRenderData requires depth_texture");

  render_data_ = data;

  // Create depth pass config and pass (persistent)
  if (!depth_pass_config_) {
    depth_pass_config_ = std::make_shared<engine::DepthPrePassConfig>();
    depth_pass_config_->debug_name = "DepthPrePass";
  }

  if (!depth_pass_) {
    depth_pass_ = std::make_shared<engine::DepthPrePass>(depth_pass_config_);
  }

  // Create shader pass config and pass (persistent)
  if (!shader_pass_config_) {
    shader_pass_config_ = std::make_shared<engine::ShaderPassConfig>();
    shader_pass_config_->debug_name = "ShaderPass";
  }

  if (!shader_pass_) {
    shader_pass_ = std::make_shared<engine::ShaderPass>(shader_pass_config_);
  }

  LOG_F(INFO,
    "[ViewRenderer] Configured: color_tex={}, depth_tex={}, wireframe={}, "
    "clear_color=({},{},{},{})",
    static_cast<const void*>(data.color_texture.get()),
    static_cast<const void*>(data.depth_texture.get()), data.wireframe,
    data.clear_color.r, data.clear_color.g, data.clear_color.b,
    data.clear_color.a);
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
  render_data_.reset();

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
  CHECK_F(render_data_.has_value(),
    "ViewRenderer::Render requires ViewRenderData to be configured");

  co_await ExecuteGraph(*render_data_, ctx, recorder);
  co_return;
}

auto ViewRenderer::ExecuteGraph(const ViewRenderData& data,
  const engine::RenderContext& ctx, graphics::CommandRecorder& recorder) const
  -> co::Co<>
{
  CHECK_F(static_cast<bool>(data.color_texture),
    "ViewRenderData requires color_texture");
  CHECK_F(static_cast<bool>(data.depth_texture),
    "ViewRenderData requires depth_texture");
  CHECK_NOTNULL_F(depth_pass_, "DepthPrePass is not initialized");
  CHECK_NOTNULL_F(shader_pass_, "ShaderPass is not initialized");

  SyncPassConfigs(data);

  LogViewInputs(ctx, data);

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

auto ViewRenderer::SyncPassConfigs(const ViewRenderData& data) const -> void
{
  CHECK_NOTNULL_F(depth_pass_config_, "DepthPrePass config missing");
  CHECK_NOTNULL_F(shader_pass_config_, "ShaderPass config missing");

  depth_pass_config_->debug_name
    = data.wireframe ? "WireframeDepthPrePass" : "DepthPrePass";
  depth_pass_config_->depth_texture = data.depth_texture;

  shader_pass_config_->debug_name
    = data.wireframe ? "WireframeShaderPass" : "ShaderPass";
  shader_pass_config_->color_texture = data.color_texture;
  shader_pass_config_->clear_color = data.clear_color;
  shader_pass_config_->fill_mode = data.wireframe
    ? graphics::FillMode::kWireFrame
    : graphics::FillMode::kSolid;
}

auto ViewRenderer::LogViewInputs(
  const engine::RenderContext& ctx, const ViewRenderData& data) const -> void
{
  const auto view_id = ctx.current_view.view_id.get();
  const auto psf = ctx.current_view.prepared_frame;
  const auto prepared_ok = psf ? psf->IsValid() : false;
  const auto draw_bytes = psf ? psf->draw_metadata_bytes.size() : 0U;

  LOG_F(INFO,
    "[ViewRenderer] Graph inputs: view_id={}, prepared={}, draws_bytes={}, "
    "wireframe={}, gui={}, color_tex={}, depth_tex={}",
    view_id, prepared_ok ? "true" : "false", draw_bytes,
    data.wireframe ? "true" : "false", data.render_gui ? "true" : "false",
    static_cast<const void*>(data.color_texture.get()),
    static_cast<const void*>(data.depth_texture.get()));
}

auto ViewRenderer::RenderDepthPrePass(const engine::RenderContext& ctx,
  graphics::CommandRecorder& recorder) const -> co::Co<>
{
  LOG_SCOPE_F(INFO, "[ViewRenderer] DepthPrePass");

  CHECK_NOTNULL_F(depth_pass_, "DepthPrePass not configured");

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

  CHECK_NOTNULL_F(shader_pass_, "ShaderPass not configured");

  // Prepare resources (transitions)
  co_await shader_pass_->PrepareResources(ctx, recorder);

  // Execute draw calls
  co_await shader_pass_->Execute(ctx, recorder);

  co_return;
}

auto ViewRenderer::RenderGui(graphics::CommandRecorder& recorder,
  const graphics::Framebuffer& framebuffer) const -> co::Co<>
{
  CHECK_F(render_data_.has_value(),
    "ViewRenderer::RenderGui requires ViewRenderData");
  if (!render_data_->render_gui) {
    co_return;
  }

  CHECK_F(static_cast<bool>(imgui_module_),
    "ImGui module required for GUI rendering");
  if (!imgui_module_->IsWitinFrameScope()) {
    LOG_F(INFO, "[ViewRenderer] ImGui frame not active; skipping GUI render");
    co_return;
  }

  const auto imgui_pass = imgui_module_->GetRenderPass();
  CHECK_F(static_cast<bool>(imgui_pass), "ImGui render pass unavailable");

  const auto& fb_desc = framebuffer.GetDescriptor();
  if (!fb_desc.color_attachments.empty()
    && fb_desc.color_attachments[0].texture) {
    recorder.RequireResourceState(*fb_desc.color_attachments[0].texture,
      graphics::ResourceStates::kRenderTarget);
    recorder.FlushBarriers();
  }

  recorder.BindFrameBuffer(framebuffer);

  co_await imgui_pass->Render(recorder);
  co_return;
}

} // namespace oxygen::examples::multiview
