//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorCompositor.h"
#include "EditorModule/EditorView.h"
#include "EditorModule/ViewManager.h"

namespace oxygen::interop::module {

  namespace {

    [[nodiscard]] auto ResolveDeclaredInitialState(
      const graphics::Texture& texture,
      const graphics::ResourceStates fallback) noexcept
      -> graphics::ResourceStates {
      auto state = texture.GetDescriptor().initial_state;
      if (state == graphics::ResourceStates::kUnknown ||
        state == graphics::ResourceStates::kUndefined) {
        state = fallback;
      }
      return state;
    }

    auto TrackTextureFromKnownOrInitial(
      graphics::CommandRecorder& recorder,
      const graphics::Texture& texture,
      const graphics::ResourceStates fallback,
      const char* usage) -> graphics::ResourceStates {
      const auto declared_state = ResolveDeclaredInitialState(texture, fallback);

      if (recorder.IsResourceTracked(texture) ||
        recorder.AdoptKnownResourceState(texture)) {
        return declared_state;
      }

      CHECK_F(declared_state != graphics::ResourceStates::kUnknown
          && declared_state != graphics::ResourceStates::kUndefined,
        "EditorCompositor: cannot track {} texture '{}' without a known or "
        "declared initial state",
        usage, texture.GetName());
      recorder.BeginTrackingResourceState(texture, declared_state);
      return declared_state;
    }

  } // namespace

  EditorCompositor::EditorCompositor(std::shared_ptr<oxygen::Graphics> graphics,
    ViewManager& view_manager,
    SurfaceRegistry& registry)
    : graphics_(std::move(graphics)), view_manager_(&view_manager),
    registry_(&registry) {
  }

  EditorCompositor::~EditorCompositor() = default;

  void EditorCompositor::EnsureFramebuffersForSurface(
    const graphics::Surface& surface) {
    if (graphics_.expired()) {
      return;
    }
    auto gfx = graphics_.lock();

    // If we already have cached framebuffers for this surface, check if they
    // match the surface size
    auto& fb_vec = surface_framebuffers_[&surface];
    if (!fb_vec.empty()) {
      // Check if resize is needed (naive check: just check first FB)
      if (fb_vec[0]) {
        const auto& desc = fb_vec[0]->GetDescriptor();
        // Check first color attachment
        if (!desc.color_attachments.empty() &&
          desc.color_attachments[0].texture) {
          const auto& tex_desc =
            desc.color_attachments[0].texture->GetDescriptor();
          if (surface.Width() > 0 && surface.Height() > 0) {
            if (tex_desc.width != static_cast<uint32_t>(surface.Width()) ||
              tex_desc.height != static_cast<uint32_t>(surface.Height())) {
              fb_vec.clear();
            }
          }
        }
      }
    }

    if (!fb_vec.empty()) {
      return;
    }

    const auto surface_width = surface.Width();
    const auto surface_height = surface.Height();
    const auto frame_count =
      static_cast<size_t>(oxygen::frame::kFramesInFlight.get());

    fb_vec.resize(frame_count);

    for (size_t i = 0; i < frame_count; ++i) {
      auto cb = surface.GetBackBuffer(static_cast<uint32_t>(i));
      if (!cb) {
        continue;
      }

      // Create depth texture matching the example's flags/format. Prefer
      // using the backbuffer's descriptor width/height (these are the
      // actual texture dimensions). Fall back to the surface reported
      // size if the descriptor reports zero (some swapchain attach
      // timing can temporarily yield zero-sized descriptors).
      oxygen::graphics::TextureDesc depth_desc;
      const auto& cb_desc = cb->GetDescriptor();
      depth_desc.width = (cb_desc.width != 0)
        ? cb_desc.width
        : static_cast<uint32_t>(surface_width);
      depth_desc.height = (cb_desc.height != 0)
        ? cb_desc.height
        : static_cast<uint32_t>(surface_height);
      depth_desc.format = oxygen::Format::kDepth32;
      depth_desc.texture_type = oxygen::TextureType::kTexture2D;
      depth_desc.is_shader_resource = true;
      depth_desc.is_render_target = true;
      depth_desc.use_clear_value = true;
      depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
      depth_desc.initial_state = oxygen::graphics::ResourceStates::kDepthWrite;

      std::shared_ptr<oxygen::graphics::Texture> depth_tex;
      try {
        depth_tex = gfx->CreateTexture(depth_desc);
      }
      catch (...) {
        LOG_F(WARNING, "EditorCompositor: CreateTexture for depth failed");
      }

      auto desc = oxygen::graphics::FramebufferDesc{}.AddColorAttachment(
        surface.GetBackBuffer(static_cast<uint32_t>(i)));
      if (depth_tex) {
        desc.SetDepthAttachment(depth_tex);
      }

      fb_vec[i] = gfx->CreateFramebuffer(desc);
    }
  }

  auto EditorCompositor::GetCurrentFramebufferForSurface(
    const graphics::Surface& surface) -> std::shared_ptr<graphics::Framebuffer> {
    EnsureFramebuffersForSurface(surface);

    const auto iter = surface_framebuffers_.find(&surface);
    if (iter == surface_framebuffers_.end()) {
      return {};
    }

    const auto index = surface.GetCurrentBackBufferIndex();
    if (index >= iter->second.size()) {
      LOG_F(WARNING,
        "EditorCompositor: current backbuffer index {} is outside framebuffer "
        "cache size {} for surface '{}'",
        index, iter->second.size(), surface.GetName());
      return {};
    }

    return iter->second[index];
  }

  void EditorCompositor::OnCompositing() {
    DLOG_SCOPE_FUNCTION(2);

    if (graphics_.expired()) {
      DLOG_F(WARNING, "Graphics instance expired, skipping compositing");
      return;
    }

    struct CompositingTask {
      graphics::Surface* surface;
      std::shared_ptr<graphics::Texture> texture;
    };
    std::vector<CompositingTask> tasks;

    // Query all registered views and determine which need compositing
    auto registered_views = view_manager_->GetAllRegisteredViews();

    for (auto* view : registered_views) {
      if (!view)
        continue;

      auto texture = view->GetColorTexture();
      if (!texture) {
        DLOG_F(1, "View '{}' has no color texture, skipping", view->GetName());
        continue;
      }

      // Only composite if view has a compositing target configured
      const auto& target = view->GetConfig().compositing_target;
      if (!target.has_value() || !target.value()) {
        DLOG_F(1, "View '{}' has no compositing target, skipping",
          view->GetName());
        continue;
      }

      // Ensure the compositing target is a live surface currently registered
      // with the SurfaceRegistry. Views may retain raw surface pointers which
      // have since been unregistered/removed; we must avoid queueing
      // compositing tasks for surfaces the engine no longer manages.
      DCHECK_NOTNULL_F(registry_);
      if (!registry_->ContainsSurface(target.value())) {
        DLOG_F(1, "View '{}' targets an unregistered surface (ptr={}); skipping",
          view->GetName(), fmt::ptr(target.value()));
        continue;
      }

      DLOG_F(2, "View '{}' ready for compositing (surface={}, texture={}x{})",
        view->GetName(), fmt::ptr(target.value()),
        texture->GetDescriptor().width, texture->GetDescriptor().height);

      tasks.push_back({ target.value(), std::move(texture) });
    }

    if (tasks.empty()) {
      DLOG_F(
        INFO,
        "No views require compositing, skipping command recorder acquisition");
      return;
    }

    LOG_F(2, "Compositing {} view(s) to surfaces", tasks.size());

    // Acquire command recorder only if we have work to do
    auto gfx = graphics_.lock();
    auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
    auto recorder = gfx->AcquireCommandRecorder(queue_key, "EditorCompositing");

    // Perform compositing for each task
    for (const auto& task : tasks) {
      // Full surface viewport
      oxygen::ViewPort viewport{
          .top_left_x = 0.0f,
          .top_left_y = 0.0f,
          .width = static_cast<float>(task.surface->Width()),
          .height = static_cast<float>(task.surface->Height()),
          .min_depth = 0.0f,
          .max_depth = 1.0f };

      DLOG_F(3, "Compositing to surface {} (viewport: {}x{})",
        fmt::ptr(task.surface), static_cast<uint32_t>(viewport.width),
        static_cast<uint32_t>(viewport.height));

      DLOG_F(3,
        "EditorCompositor: Compositing view texture {}x{} to surface {}x{}",
        task.texture->GetDescriptor().width,
        task.texture->GetDescriptor().height,
        static_cast<uint32_t>(viewport.width),
        static_cast<uint32_t>(viewport.height));

      CompositeToSurface(*recorder, *task.surface, *task.texture, viewport);
    }
  }

  void EditorCompositor::CompositeToSurface(
    graphics::CommandRecorder& recorder, const graphics::Surface& surface,
    const graphics::Texture& source_texture,
    const ViewPort& destination_region) {
    DLOG_SCOPE_FUNCTION(3);
    DLOG_F(3, "source texture {} '{}'", fmt::ptr(&source_texture),
      source_texture.GetName());
    DLOG_F(3, "target surface {} '{}'", fmt::ptr(&surface), surface.GetName());

    // Get the current backbuffer for the surface
    auto backbuffer = surface.GetCurrentBackBuffer();
    if (!backbuffer) {
      return;
    }

    // The viewport color texture was just produced by Vortex, so the graphics
    // queue's known state is authoritative. The descriptor initial state is
    // only a fallback for resources not seen by this queue yet.
    const auto& src_desc = source_texture.GetDescriptor();
    const auto src_final = TrackTextureFromKnownOrInitial(recorder,
      source_texture, graphics::ResourceStates::kCommon, "source");
    DLOG_F(3,
      "begin tracking source: final={} (shader_resource={}, "
      "render_target={})",
      src_final, src_desc.is_shader_resource, src_desc.is_render_target);

    // Transition source to CopySource
    DLOG_F(3, "transition source: -> {}", graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(source_texture,
      graphics::ResourceStates::kCopySource);

    auto dst_desc = backbuffer->GetDescriptor();
    TrackTextureFromKnownOrInitial(recorder, *backbuffer,
      graphics::ResourceStates::kPresent, "target");
    DLOG_F(3, "begin tracking target (size={}x{})",
      dst_desc.width, dst_desc.height);

    // Transition backbuffer to CopyDest
    DLOG_F(3, "transition backbuffer: -> {}",
      graphics::ResourceStates::kCopyDest);
    recorder.RequireResourceState(*backbuffer,
      graphics::ResourceStates::kCopyDest);

    // Flush barriers before copy
    recorder.FlushBarriers();

    uint32_t copy_width = std::min(src_desc.width, dst_desc.width);
    uint32_t copy_height = std::min(src_desc.height, dst_desc.height);

    // Sanity check: log if sizes don't match
    if (copy_width != src_desc.width || copy_height != src_desc.height ||
      copy_width != dst_desc.width || copy_height != dst_desc.height) {
      LOG_F(WARNING,
        "Size mismatch during copy. Src: {}x{}, Dst: {}x{}, Copy: {}x{}",
        src_desc.width, src_desc.height, dst_desc.width, dst_desc.height,
        copy_width, copy_height);
    }

    // Blit (CopyTexture)
    graphics::TextureSlice src_slice{
        .x = 0,
        .y = 0,
        .z = 0,
        .width = copy_width,
        .height = copy_height,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
    };

    graphics::TextureSubResourceSet src_sub{
        .base_mip_level = 0,
        .num_mip_levels = 1,
        .base_array_slice = 0,
        .num_array_slices = 1,
    };

    graphics::TextureSlice dst_slice{
        .x = 0,
        .y = 0,
        .z = 0,
        .width = copy_width,
        .height = copy_height,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
    };

    graphics::TextureSubResourceSet dst_sub{
        .base_mip_level = 0,
        .num_mip_levels = 1,
        .base_array_slice = 0,
        .num_array_slices = 1,
    };

    recorder.CopyTexture(source_texture, src_slice, src_sub, *backbuffer,
      dst_slice, dst_sub);

    // Return the source to its declared steady state for consumers after the
    // editor blit. If Vortex renders it again next frame, that pass will adopt
    // this known state and transition from here.
    DLOG_F(3, "transition source: -> {}", src_final);
    recorder.RequireResourceStateFinal(source_texture, src_final);

    // Transition backbuffer to Present
    DLOG_F(3, "transition target: -> {}", graphics::ResourceStates::kPresent);
    recorder.RequireResourceStateFinal(*backbuffer,
      graphics::ResourceStates::kPresent);

    // Flush barriers after transitions
    recorder.FlushBarriers();
  }

  void EditorCompositor::CleanupSurface(const graphics::Surface& surface) {
    surface_framebuffers_.erase(&surface);
  }

} // namespace oxygen::interop::module
