//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>

#include "DemoShell/Runtime/Internal/ForwardPipelineImpl.h"
#include "DemoShell/Runtime/Internal/ViewLifecycleService.h"

namespace oxygen::examples::internal {

struct ViewLifecycleService::State {
  std::map<ViewId, CompositionViewImpl> view_pool;
  std::vector<CompositionViewImpl*> sorted_views;
};

ViewLifecycleService::ViewLifecycleService(
  engine::Renderer& renderer, RenderViewCoroutine render_view_coroutine)
  : renderer_(observer_ptr { &renderer })
  , render_view_coroutine_(std::move(render_view_coroutine))
  , state_(std::make_unique<State>())
{
  DCHECK_NOTNULL_F(render_view_coroutine_);
}

ViewLifecycleService::~ViewLifecycleService() = default;

void ViewLifecycleService::SyncActiveViews(engine::FrameContext& context,
  std::span<const CompositionView> view_descs,
  observer_ptr<graphics::Framebuffer> composite_target, Graphics& graphics)
{
  state_->sorted_views.clear();
  state_->sorted_views.reserve(view_descs.size());

  const auto frame_seq = context.GetFrameSequenceNumber();
  uint32_t index = 0;
  for (auto desc : view_descs) { // Copy so we can normalize viewport.
    if (desc.view.viewport.width <= 0 || desc.view.viewport.height <= 0) {
      if (composite_target != nullptr) {
        const auto& fb_desc = composite_target->GetDescriptor();
        if (!fb_desc.color_attachments.empty()
          && fb_desc.color_attachments[0].texture) {
          desc.view.viewport.width = static_cast<float>(
            fb_desc.color_attachments[0].texture->GetDescriptor().width);
          desc.view.viewport.height = static_cast<float>(
            fb_desc.color_attachments[0].texture->GetDescriptor().height);
        }
      } else {
        desc.view.viewport.width = 1280.0f;
        desc.view.viewport.height = 720.0f;
      }
    }

    auto& view_impl = state_->view_pool[desc.id];
    view_impl.Sync(desc, index++, frame_seq);
    view_impl.EnsureResources(graphics);
    state_->sorted_views.push_back(&view_impl);
  }

  std::stable_sort(state_->sorted_views.begin(), state_->sorted_views.end(),
    [](const CompositionViewImpl* a, const CompositionViewImpl* b) {
      if (a->intent.z_order != b->intent.z_order) {
        return a->intent.z_order < b->intent.z_order;
      }
      return a->submission_index < b->submission_index;
    });
}

void ViewLifecycleService::RegisterViewRenderGraph(CompositionViewImpl& view)
{
  DCHECK_NOTNULL_F(renderer_.get());
  const auto registered_view_id = view.registered_view_id;
  auto camera = view.intent.camera.value_or(scene::SceneNode {});
  renderer::SceneCameraViewResolver resolver(
    [camera](const ViewId&) -> scene::SceneNode { return camera; });
  renderer_->RegisterViewRenderGraph(
    registered_view_id, render_view_coroutine_, resolver(registered_view_id));

  view.registered_with_renderer = true;
}

void ViewLifecycleService::PublishViews(engine::FrameContext& context)
{
  DCHECK_NOTNULL_F(renderer_.get());
  for (auto* view : state_->sorted_views) {
    engine::ViewContext view_ctx;
    view_ctx.view = view->intent.view;
    const bool has_scene = view->intent.camera.has_value();
    view_ctx.metadata = { .name = std::string(view->intent.name),
      .purpose = has_scene ? "scene" : "overlay",
      .with_atmosphere = view->intent.with_atmosphere };
    view_ctx.render_target = view->hdr_framebuffer
      ? observer_ptr { view->hdr_framebuffer.get() }
      : observer_ptr { view->sdr_framebuffer.get() };
    view_ctx.composite_source = view->sdr_framebuffer
      ? observer_ptr { view->sdr_framebuffer.get() }
      : view_ctx.render_target;

    CHECK_F(!has_scene || view->intent.enable_hdr,
      "Scene view '{}' must enable HDR rendering", view->intent.name);
    CHECK_NOTNULL_F(view_ctx.render_target.get(),
      "View '{}' missing render_target framebuffer", view->intent.name);
    CHECK_NOTNULL_F(view_ctx.composite_source.get(),
      "View '{}' missing composite_source framebuffer", view->intent.name);
    if (has_scene) {
      CHECK_NOTNULL_F(view->hdr_framebuffer.get(),
        "Scene view '{}' missing HDR framebuffer", view->intent.name);
      CHECK_NOTNULL_F(view->sdr_framebuffer.get(),
        "Scene view '{}' missing SDR framebuffer", view->intent.name);
    }

    if (view->registered_view_id == kInvalidViewId) {
      view->registered_view_id = context.RegisterView(std::move(view_ctx));
      LOG_F(INFO,
        "Registered View '{}' (IntentID: {}) with Engine "
        "(RegisteredViewId: {})",
        view->intent.name, view->intent.id.get(),
        view->registered_view_id.get());
    } else {
      context.UpdateView(view->registered_view_id, std::move(view_ctx));
      DLOG_F(1, "Updated View '{}' (RegisteredViewId: {})", view->intent.name,
        view->registered_view_id.get());
    }
  }
}

void ViewLifecycleService::RegisterRenderGraphs()
{
  DCHECK_NOTNULL_F(renderer_.get());
  for (auto* view : state_->sorted_views) {
    RegisterViewRenderGraph(*view);
  }
}

void ViewLifecycleService::UnpublishStaleViews(engine::FrameContext& context)
{
  DCHECK_NOTNULL_F(renderer_.get());
  const auto current_frame = context.GetFrameSequenceNumber();
  static constexpr frame::SequenceNumber kMaxIdleFrames { 60 };
  for (auto it = state_->view_pool.begin(); it != state_->view_pool.end();) {
    if (current_frame - it->second.last_seen_frame > kMaxIdleFrames) {
      LOG_F(INFO, "Reaping View resources for ID {}", it->first);

      if (it->second.registered_view_id != kInvalidViewId) {
        LOG_F(INFO,
          "Unpublishing View '{}' (RegisteredViewId: {}) from Engine and "
          "Renderer",
          it->second.intent.name, it->second.registered_view_id.get());
        context.RemoveView(it->second.registered_view_id);
        renderer_->UnregisterViewRenderGraph(it->second.registered_view_id);
      }

      it = state_->view_pool.erase(it);
    } else {
      ++it;
    }
  }
}

auto ViewLifecycleService::GetOrderedActiveViews() const
  -> const std::vector<CompositionViewImpl*>&
{
  return state_->sorted_views;
}

} // namespace oxygen::examples::internal
