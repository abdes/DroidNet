//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "EditorModule/EditorCompositor.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>

#include "EditorModule/ViewManager.h"
#include "EditorModule/EditorView.h"

namespace oxygen::interop::module {

EditorCompositor::EditorCompositor(std::shared_ptr<oxygen::Graphics> graphics,
                                   ViewManager& view_manager)
    : graphics_(std::move(graphics))
    , view_manager_(&view_manager)
{
}

EditorCompositor::~EditorCompositor() = default;

void EditorCompositor::EnsureFramebuffersForSurface(
    const graphics::Surface& surface)
{
  if (graphics_.expired()) {
    return;
  }
  auto gfx = graphics_.lock();

  // If we already have cached framebuffers for this surface, check if they match the surface size
  auto& fb_vec = surface_framebuffers_[&surface];
  if (!fb_vec.empty()) {
    // Check if resize is needed (naive check: just check first FB)
    if (fb_vec[0]) {
      const auto& desc = fb_vec[0]->GetDescriptor();
      // Check first color attachment
      if (!desc.color_attachments.empty() && desc.color_attachments[0].texture) {
        const auto& tex_desc = desc.color_attachments[0].texture->GetDescriptor();
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

void EditorCompositor::OnCompositing()
{
  LOG_SCOPE_F(1, "EditorCompositor::OnCompositing");


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
  DLOG_F(INFO, "Checking {} registered views for compositing", registered_views.size());

  for (auto* view : registered_views) {
    if (!view) continue;

    auto texture = view->GetColorTexture();
    if (!texture) {
      DLOG_F(2, "View '{}' has no color texture, skipping",
             view->GetConfig().name);
      continue;
    }

    // Only composite if view has a compositing target configured
    const auto& target = view->GetConfig().compositing_target;
    if (!target.has_value() || !target.value()) {
      DLOG_F(2, "View '{}' has no compositing target, skipping",
             view->GetConfig().name);
      continue;
    }

    DLOG_F(INFO, "View '{}' ready for compositing (surface={}, texture={}x{})",
           view->GetConfig().name,
           fmt::ptr(target.value()),
           texture->GetDescriptor().width,
           texture->GetDescriptor().height);

    tasks.push_back({ target.value(), std::move(texture) });
  }

  if (tasks.empty()) {
    DLOG_F(1, "No views require compositing, skipping command recorder acquisition");
    return;
  }

  LOG_F(INFO, "Compositing {} view(s) to surfaces", tasks.size());

  // Acquire command recorder only if we have work to do
  auto gfx = graphics_.lock();
  auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "EditorCompositing");

  // Perform compositing for each task
  for (const auto& task : tasks) {
    // Full surface viewport
    oxygen::ViewPort viewport {
      .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = static_cast<float>(task.surface->Width()),
      .height = static_cast<float>(task.surface->Height()),
      .min_depth = 0.0f,
      .max_depth = 1.0f
    };

    DLOG_F(INFO, "Compositing to surface {} (viewport: {}x{})",
           fmt::ptr(task.surface),
           static_cast<uint32_t>(viewport.width),
           static_cast<uint32_t>(viewport.height));

    CompositeToSurface(*recorder, *task.surface, *task.texture, viewport);
  }

  LOG_F(INFO, "Compositing complete, command recorder will submit on destruction");
}

void EditorCompositor::CompositeToSurface(
    graphics::CommandRecorder& recorder, const graphics::Surface& surface,
    const graphics::Texture& source_texture, const ViewPort& destination_region)
{
  // Get the current backbuffer for the surface
  auto backbuffer = surface.GetCurrentBackBuffer();
  if (!backbuffer) {
    return;
  }

  // Track source texture state using the texture's descriptor initial state
  // (falls back to Common if unspecified). This keeps the command-recording
  // resource tracker consistent with how the texture was created.
  try {
    const auto& src_desc = source_texture.GetDescriptor();
    auto src_initial = src_desc.initial_state;
    if (src_initial == graphics::ResourceStates::kUnknown || src_initial == graphics::ResourceStates::kUndefined) {
      src_initial = graphics::ResourceStates::kCommon;
    }
    recorder.BeginTrackingResourceState(source_texture, src_initial);
  } catch (...) {
    /* ignore if already tracked or tracking not supported */
  }

  // Transition source to CopySource
  recorder.RequireResourceState(source_texture, graphics::ResourceStates::kCopySource);

  // Ensure the recorder is tracking the backbuffer's current state first.
  // Some backbuffers may have been used earlier in this command list (e.g.
  // as shader resources) and the state tracker needs to know the actual
  // starting state. Use the backbuffer descriptor's initial_state when
  // available, otherwise assume Present as a safe default for swapchain
  // images.
  try {
    auto bb_desc = backbuffer->GetDescriptor();
    auto bb_initial = bb_desc.initial_state;
    if (bb_initial == graphics::ResourceStates::kUnknown || bb_initial == graphics::ResourceStates::kUndefined) {
      bb_initial = graphics::ResourceStates::kPresent;
    }
    recorder.BeginTrackingResourceState(*backbuffer, bb_initial);
  } catch (...) {
    // Ignore if already tracked or other issues; RequireResourceState will
    // validate/transition relative to the tracked state.
  }

  // Transition backbuffer to CopyDest
  recorder.RequireResourceState(*backbuffer, graphics::ResourceStates::kCopyDest);

  // Flush barriers before copy
  recorder.FlushBarriers();

  // Blit (CopyTexture)
  graphics::TextureSlice src_slice {
    .x = 0,
    .y = 0,
    .z = 0,
    .width = source_texture.GetDescriptor().width,
    .height = source_texture.GetDescriptor().height,
    .depth = 1,
    .mip_level = 0,
    .array_slice = 0
  };

  graphics::TextureSubResourceSet src_sub {
    .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1
  };

  graphics::TextureSlice dst_slice {
    .x = 0,
    .y = 0,
    .z = 0,
    .width = backbuffer->GetDescriptor().width,
    .height = backbuffer->GetDescriptor().height,
    .depth = 1,
    .mip_level = 0,
    .array_slice = 0
  };

  graphics::TextureSubResourceSet dst_sub {
    .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1
  };

  recorder.CopyTexture(source_texture, src_slice, src_sub, *backbuffer, dst_slice, dst_sub);

  // Reset source texture to its neutral state (use descriptor if available,
  // otherwise fallback to Common). Using the descriptor keeps semantics
  // consistent with how the texture was created and avoids incorrect
  // transition assumptions.
  try {
    const auto& src_desc = source_texture.GetDescriptor();
    auto src_final = src_desc.initial_state;
    if (src_final == graphics::ResourceStates::kUnknown || src_final == graphics::ResourceStates::kUndefined) {
      src_final = graphics::ResourceStates::kCommon;
    }
    recorder.RequireResourceState(source_texture, src_final);
  } catch (...) {
    // fall back to common if anything goes wrong
    recorder.RequireResourceState(source_texture, graphics::ResourceStates::kCommon);
  }

  // Transition backbuffer to Present
  recorder.RequireResourceState(*backbuffer, graphics::ResourceStates::kPresent);

  // Flush barriers after transitions
  recorder.FlushBarriers();
}

void EditorCompositor::CleanupSurface(const graphics::Surface& surface)
{
  surface_framebuffers_.erase(&surface);
}

} // namespace oxygen::interop::module
