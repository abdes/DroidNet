#include "Unmanaged/RenderGraph.h"
//===----------------------------------------------------------------------===//
// RenderGraph implementation (copied/adapted from Examples/Common)
//===----------------------------------------------------------------------===//

#include "Unmanaged/RenderGraph.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/OxCo/Co.h>

using namespace oxygen;

namespace Oxygen::Editor::EngineInterface {

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
    shader_pass_config_->clear_color = graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F };
    shader_pass_config_->debug_name = "ShaderPass";
  }
  if (!shader_pass_) {
    shader_pass_ = std::make_shared<engine::ShaderPass>(shader_pass_config_);
  }

  // Transparent pass
  if (!transparent_pass_config_) {
    transparent_pass_config_ = std::make_shared<engine::TransparentPass::Config>();
    transparent_pass_config_->debug_name = "TransparentPass";
  }
  if (!transparent_pass_) {
    transparent_pass_ = std::make_shared<engine::TransparentPass>(transparent_pass_config_);
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

  if (render_context_.framebuffer) {
    DLOG_F(2, "RenderGraph: clearing cached framebuffer to avoid pinning backbuffers");
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
      transparent_pass_config_->color_texture = desc.color_attachments[0].texture;
    else
      transparent_pass_config_->color_texture.reset();

    if (desc.depth_attachment.IsValid())
      transparent_pass_config_->depth_texture = desc.depth_attachment.texture;
    else
      transparent_pass_config_->depth_texture.reset();
  }

  if (depth_pass_config_) {
    if (desc.depth_attachment.IsValid()) {
      depth_pass_config_->depth_texture = desc.depth_attachment.texture;
    } else {
      depth_pass_config_->depth_texture.reset();
    }
  }
}

auto RenderGraph::RunPasses(const oxygen::engine::RenderContext& ctx,
  oxygen::graphics::CommandRecorder& recorder) -> co::Co<>
{
  // Depth Pre-Pass execution
  if (depth_pass_) {
    try {
      DLOG_F(INFO, "RenderGraph: running DepthPrePass (depth_texture_valid={})",
             static_cast<bool>(depth_pass_config_ && depth_pass_config_->depth_texture));
      co_await depth_pass_->PrepareResources(ctx, recorder);
      co_await depth_pass_->Execute(ctx, recorder);
      DLOG_F(INFO, "RenderGraph: DepthPrePass completed successfully");
    } catch (const std::exception& e) {
      DLOG_F(WARNING, "RenderGraph: DepthPrePass threw: {}", e.what());
    } catch (...) {
      DLOG_F(WARNING, "RenderGraph: DepthPrePass threw unknown exception");
    }
  }

  // Shader Pass execution
  if (shader_pass_) {
    try {
      DLOG_F(INFO, "RenderGraph: running ShaderPass (color_texture_valid={})",
             static_cast<bool>(shader_pass_config_ && shader_pass_config_->color_texture));
      co_await shader_pass_->PrepareResources(ctx, recorder);
      co_await shader_pass_->Execute(ctx, recorder);
      DLOG_F(INFO, "RenderGraph: ShaderPass completed successfully");
    } catch (const std::exception& e) {
      DLOG_F(WARNING, "RenderGraph: ShaderPass threw: {}", e.what());
    } catch (...) {
      DLOG_F(WARNING, "RenderGraph: ShaderPass threw unknown exception");
    }
  }

  // Transparent Pass execution
  if (transparent_pass_) {
    try {
      DLOG_F(INFO, "RenderGraph: running TransparentPass (color_valid={} depth_valid={})",
             static_cast<bool>(transparent_pass_config_ && transparent_pass_config_->color_texture),
             static_cast<bool>(transparent_pass_config_ && transparent_pass_config_->depth_texture));
      co_await transparent_pass_->PrepareResources(ctx, recorder);
      co_await transparent_pass_->Execute(ctx, recorder);
      DLOG_F(INFO, "RenderGraph: TransparentPass completed successfully");
    } catch (const std::exception& e) {
      DLOG_F(WARNING, "RenderGraph: TransparentPass threw: {}", e.what());
    } catch (...) {
      DLOG_F(WARNING, "RenderGraph: TransparentPass threw unknown exception");
    }
  }

  co_return;
}

} // namespace Oxygen::Editor::EngineInterface
