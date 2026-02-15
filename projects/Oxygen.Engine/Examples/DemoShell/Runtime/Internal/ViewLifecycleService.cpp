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

#include "DemoShell/Runtime/Internal/CompositionViewImpl.h"
#include "DemoShell/Runtime/Internal/ViewLifecycleService.h"

namespace oxygen::examples::internal {

#if !defined(OXYGEN_ENGINE_TESTING)
auto access::ViewLifecycleTagFactory::Get() noexcept -> ViewLifecycleAccessTag
{
  return ViewLifecycleAccessTag {};
}
#endif // !defined(OXYGEN_ENGINE_TESTING)

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
        } else {
          CHECK_F(false,
            "View '{}' has invalid viewport and composite target has no "
            "resolvable color attachment extent",
            desc.name);
        }
      } else {
        CHECK_F(false,
          "View '{}' has invalid viewport and no composite target was "
          "provided to resolve extent",
          desc.name);
      }
    }

    auto& view_impl = state_->view_pool[desc.id];
    view_impl.PrepareForRender(desc, index++, frame_seq, graphics,
      access::ViewLifecycleTagFactory::Get());
    state_->sorted_views.push_back(&view_impl);
  }

  std::stable_sort(state_->sorted_views.begin(), state_->sorted_views.end(),
    [](const CompositionViewImpl* a, const CompositionViewImpl* b) {
      if (a->GetDescriptor().z_order != b->GetDescriptor().z_order) {
        return a->GetDescriptor().z_order < b->GetDescriptor().z_order;
      }
      return a->GetSubmissionOrder() < b->GetSubmissionOrder();
    });
}

void ViewLifecycleService::RegisterViewRenderGraph(CompositionViewImpl& view)
{
  DCHECK_NOTNULL_F(renderer_.get());
  const auto published_view_id = view.GetPublishedViewId();
  CHECK_F(published_view_id != kInvalidViewId,
    "RegisterViewRenderGraph called for unpublished view '{}'",
    view.GetDescriptor().name);
  auto camera = view.GetDescriptor().camera.value_or(scene::SceneNode {});
  renderer::SceneCameraViewResolver resolver(
    [camera](const ViewId&) -> scene::SceneNode { return camera; });
  renderer_->RegisterViewRenderGraph(
    published_view_id, render_view_coroutine_, resolver(published_view_id));
}

void ViewLifecycleService::PublishViews(engine::FrameContext& context)
{
  DCHECK_NOTNULL_F(renderer_.get());
  for (auto* view : state_->sorted_views) {
    engine::ViewContext view_ctx;
    view_ctx.view = view->GetDescriptor().view;
    const bool has_scene = view->GetDescriptor().camera.has_value();
    view_ctx.metadata = { .name = std::string(view->GetDescriptor().name),
      .purpose = has_scene ? "scene" : "overlay",
      .with_atmosphere = view->GetDescriptor().with_atmosphere };
    view_ctx.render_target = view->GetHdrFramebuffer()
      ? observer_ptr { view->GetHdrFramebuffer().get() }
      : observer_ptr { view->GetSdrFramebuffer().get() };
    view_ctx.composite_source = view->GetSdrFramebuffer()
      ? observer_ptr { view->GetSdrFramebuffer().get() }
      : view_ctx.render_target;

    CHECK_F(!has_scene || view->GetDescriptor().enable_hdr,
      "Scene view '{}' must enable HDR rendering", view->GetDescriptor().name);
    CHECK_NOTNULL_F(view_ctx.render_target.get(),
      "View '{}' missing render_target framebuffer",
      view->GetDescriptor().name);
    CHECK_NOTNULL_F(view_ctx.composite_source.get(),
      "View '{}' missing composite_source framebuffer",
      view->GetDescriptor().name);
    if (has_scene) {
      CHECK_NOTNULL_F(view->GetHdrFramebuffer().get(),
        "Scene view '{}' missing HDR framebuffer", view->GetDescriptor().name);
      CHECK_NOTNULL_F(view->GetSdrFramebuffer().get(),
        "Scene view '{}' missing SDR framebuffer", view->GetDescriptor().name);
    }

    if (view->GetPublishedViewId() == kInvalidViewId) {
      view->SetPublishedViewId(context.RegisterView(std::move(view_ctx)),
        access::ViewLifecycleTagFactory::Get());
      LOG_F(INFO,
        "Registered View '{}' (IntentID: {}) with Engine "
        "(PublishedViewId: {})",
        view->GetDescriptor().name, view->GetDescriptor().id.get(),
        view->GetPublishedViewId().get());
    } else {
      context.UpdateView(view->GetPublishedViewId(), std::move(view_ctx));
      DLOG_F(1, "Updated View '{}' (PublishedViewId: {})",
        view->GetDescriptor().name, view->GetPublishedViewId().get());
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
    if (current_frame - it->second.GetLastSeenFrame() > kMaxIdleFrames) {
      LOG_F(INFO, "Reaping View resources for ID {}", it->first);

      if (it->second.GetPublishedViewId() != kInvalidViewId) {
        LOG_F(INFO,
          "Unpublishing View '{}' (PublishedViewId: {}) from Engine and "
          "Renderer",
          it->second.GetDescriptor().name,
          it->second.GetPublishedViewId().get());
        context.RemoveView(it->second.GetPublishedViewId());
        renderer_->UnregisterViewRenderGraph(it->second.GetPublishedViewId());
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
