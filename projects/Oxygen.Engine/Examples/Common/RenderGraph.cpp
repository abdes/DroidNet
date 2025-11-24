//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraph.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/OxCo/Co.h>

using namespace oxygen;

namespace oxygen::examples::common {

RenderGraph::RenderGraph(const AsyncEngineApp&) noexcept
{
  // Nothing to do eagerly — pass objects are created lazily in
  // SetupRenderPasses() on demand. Keeping construction cheap allows
  // examples to add this component early without heavy work.
}

auto RenderGraph::SetupRenderPasses() -> void
{
  LOG_SCOPE_F(3, "RenderGraph::SetupRenderPasses");

  // DepthPrePass
  if (!depth_pass_config_) {
    depth_pass_config_ = std::make_shared<engine::DepthPrePassConfig>();
    depth_pass_config_->debug_name = "DepthPrePass";
  }
  if (!depth_pass_) {
    depth_pass_ = std::make_shared<engine::DepthPrePass>(depth_pass_config_);
  }

  // Shader pass
  if (!shader_pass_config_) {
    shader_pass_config_ = std::make_shared<engine::ShaderPassConfig>();
    shader_pass_config_->clear_color
      = graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F };
    shader_pass_config_->debug_name = "ShaderPass";
  }
  if (!shader_pass_) {
    shader_pass_ = std::make_shared<engine::ShaderPass>(shader_pass_config_);
  }

  // Transparent pass
  if (!transparent_pass_config_) {
    transparent_pass_config_
      = std::make_shared<engine::TransparentPass::Config>();
    transparent_pass_config_->debug_name = "TransparentPass";
  }
  if (!transparent_pass_) {
    transparent_pass_
      = std::make_shared<engine::TransparentPass>(transparent_pass_config_);
  }
}

auto RenderGraph::ClearBackbufferReferences() -> void
{
  LOG_SCOPE_F(4, "RenderGraph::ClearBackbufferReferences");

  if (transparent_pass_config_) {
    transparent_pass_config_->color_texture.reset();
    transparent_pass_config_->depth_texture.reset();
  }

  if (shader_pass_config_) {
    shader_pass_config_->color_texture.reset();
  }

  // Drop our shared framebuffer ref so we don't pin backbuffer resources
  // across a ResizeBuffers call in AppWindow.
  if (render_context_.framebuffer) {
    DLOG_F(2,
      "RenderGraph: clearing cached framebuffer to avoid pinning backbuffers");
    render_context_.framebuffer.reset();
  }
}

auto RenderGraph::PrepareForRenderFrame(
  const std::shared_ptr<oxygen::graphics::Framebuffer>& fb) -> void
{
  LOG_SCOPE_F(4, "RenderGraph::PrepareForRenderFrame");

  if (!fb) {
    return;
  }

  // Place the active framebuffer into the reusable RenderContext
  render_context_.framebuffer = fb;

  // Assign per-pass attachments that map to the swapchain back-buffer.
  const auto& desc = fb->GetDescriptor();
  if (shader_pass_config_) {
    if (!desc.color_attachments.empty())
      shader_pass_config_->color_texture = desc.color_attachments[0].texture;
    else
      shader_pass_config_->color_texture.reset();
  }

  if (transparent_pass_config_) {
    if (!desc.color_attachments.empty())
      transparent_pass_config_->color_texture
        = desc.color_attachments[0].texture;
    else
      transparent_pass_config_->color_texture.reset();

    if (desc.depth_attachment.IsValid())
      transparent_pass_config_->depth_texture = desc.depth_attachment.texture;
    else
      transparent_pass_config_->depth_texture.reset();
  }
}

auto RenderGraph::RunPasses(const oxygen::engine::RenderContext& ctx,
  oxygen::graphics::CommandRecorder& recorder) -> co::Co<>
{
  // Depth Pre-Pass execution
  if (depth_pass_) {
    co_await depth_pass_->PrepareResources(ctx, recorder);
    co_await depth_pass_->Execute(ctx, recorder);
  }

  // Shader Pass execution
  if (shader_pass_) {
    co_await shader_pass_->PrepareResources(ctx, recorder);
    co_await shader_pass_->Execute(ctx, recorder);
  }

  // Transparent Pass execution
  if (transparent_pass_) {
    co_await transparent_pass_->PrepareResources(ctx, recorder);
    co_await transparent_pass_->Execute(ctx, recorder);
  }

  co_return;
}

} // namespace oxygen::examples::common
