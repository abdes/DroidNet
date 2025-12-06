//===----------------------------------------------------------------------===//
// RenderGraph copied from Examples/Common and adapted to the Editor module.
// Provides DepthPrePass, ShaderPass, TransparentPass, and a reusable
// per-frame RenderContext plus helpers for configuring per-frame
// attachments and running the pass sequence.
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/RenderGraph.h"

namespace oxygen::interop::module {

  auto RenderGraph::SetupRenderPasses() -> void {
    DLOG_SCOPE_FUNCTION(3);

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
      shader_pass_config_->debug_name = "ShaderPass";
    }
    if (!shader_pass_) {
      shader_pass_ = std::make_shared<engine::ShaderPass>(shader_pass_config_);
    }

    // Transparent pass
    if (!transparent_pass_config_) {
      transparent_pass_config_ =
        std::make_shared<engine::TransparentPass::Config>();
      transparent_pass_config_->debug_name = "TransparentPass";
    }
    if (!transparent_pass_) {
      transparent_pass_ =
        std::make_shared<engine::TransparentPass>(transparent_pass_config_);
    }
  }

  auto RenderGraph::ClearBackbufferReferences() -> void {
    DLOG_SCOPE_FUNCTION(3);

    if (transparent_pass_config_) {
      transparent_pass_config_->color_texture.reset();
      transparent_pass_config_->depth_texture.reset();
    }

    if (shader_pass_config_) {
      shader_pass_config_->color_texture.reset();
    }

    if (render_context_.framebuffer) {
      DLOG_F(4, "RenderGraph: clearing cached framebuffer to avoid pinning "
        "backbuffers");
      render_context_.framebuffer.reset();
    }
  }

  auto RenderGraph::PrepareForRenderFrame(
    oxygen::observer_ptr<const oxygen::graphics::Framebuffer> fb) -> void {
    DLOG_SCOPE_FUNCTION(3);

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

    if (shader_pass_config_ && shader_pass_config_->color_texture) {
      const auto& tex = *shader_pass_config_->color_texture;
      const auto& td = tex.GetDescriptor();
      LOG_F(4,
        "RenderGraph: bound shader pass color_texture {} (initial={} "
        "use_clear={})",
        static_cast<const void*>(&tex), nostd::to_string(td.initial_state),
        td.use_clear_value);
    }

    if (transparent_pass_config_) {
      if (!desc.color_attachments.empty())
        transparent_pass_config_->color_texture =
        desc.color_attachments[0].texture;
      else
        transparent_pass_config_->color_texture.reset();

      if (desc.depth_attachment.IsValid())
        transparent_pass_config_->depth_texture = desc.depth_attachment.texture;
      else
        transparent_pass_config_->depth_texture.reset();
    }

    if (transparent_pass_config_ && transparent_pass_config_->color_texture) {
      const auto& tex = *transparent_pass_config_->color_texture;
      const auto& td = tex.GetDescriptor();
      LOG_F(4,
        "RenderGraph: bound transparent pass color_texture {} (initial={} "
        "use_clear={})",
        static_cast<const void*>(&tex), nostd::to_string(td.initial_state),
        td.use_clear_value);
    }

    if (depth_pass_config_) {
      if (desc.depth_attachment.IsValid()) {
        depth_pass_config_->depth_texture = desc.depth_attachment.texture;
      }
      else {
        depth_pass_config_->depth_texture.reset();
      }
    }
  }

  auto RenderGraph::RunPasses(const oxygen::engine::RenderContext& ctx,
    oxygen::graphics::CommandRecorder& recorder)
    -> co::Co<> {
    // Depth Pre-Pass execution
    if (depth_pass_) {
      try {
        DLOG_F(3, "RenderGraph: Running DepthPrePass (depth_texture_valid={})",
          static_cast<bool>(depth_pass_config_ &&
            depth_pass_config_->depth_texture));
        co_await depth_pass_->PrepareResources(ctx, recorder);
        co_await depth_pass_->Execute(ctx, recorder);
      }
      catch (const std::exception& e) {
        DLOG_F(WARNING, "RenderGraph: DepthPrePass threw: {}", e.what());
      }
      catch (...) {
        DLOG_F(WARNING, "RenderGraph: DepthPrePass threw unknown exception");
      }
    }

    // Shader Pass execution
    if (shader_pass_) {
      try {
        DLOG_F(3, "RenderGraph: running ShaderPass (color_texture_valid={})",
          static_cast<bool>(shader_pass_config_ &&
            shader_pass_config_->color_texture));
        co_await shader_pass_->PrepareResources(ctx, recorder);
        co_await shader_pass_->Execute(ctx, recorder);
      }
      catch (const std::exception& e) {
        DLOG_F(WARNING, "RenderGraph: ShaderPass threw: {}", e.what());
      }
      catch (...) {
        DLOG_F(WARNING, "RenderGraph: ShaderPass threw unknown exception");
      }
    }

    // Transparent Pass execution
    if (transparent_pass_) {
      try {
        DLOG_F(3,
          "RenderGraph: running TransparentPass (color_valid={} "
          "depth_valid={})",
          static_cast<bool>(transparent_pass_config_ &&
            transparent_pass_config_->color_texture),
          static_cast<bool>(transparent_pass_config_ &&
            transparent_pass_config_->depth_texture));
        co_await transparent_pass_->PrepareResources(ctx, recorder);
        co_await transparent_pass_->Execute(ctx, recorder);
      }
      catch (const std::exception& e) {
        DLOG_F(WARNING, "RenderGraph: TransparentPass threw: {}", e.what());
      }
      catch (...) {
        DLOG_F(WARNING, "RenderGraph: TransparentPass threw unknown exception");
      }
    }

    co_return;
  }

} // namespace oxygen::interop::module
