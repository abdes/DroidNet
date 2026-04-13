//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <map>
#include <unordered_map>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Vortex/Internal/CompositionViewImpl.h>
#include <Oxygen/Vortex/Internal/ViewLifecycleService.h>

namespace oxygen::vortex::internal {

namespace {

auto ResolveViewForCameraNode(scene::SceneNode& camera_node,
  std::optional<oxygen::ViewPort> viewport_override) -> ResolvedView
{
  if (!camera_node.IsAlive() || !camera_node.HasCamera()) {
    ResolvedView::Params params;
    params.view_config = View {};
    params.view_matrix = Mat4(1.0F);
    params.proj_matrix = Mat4(1.0F);
    params.depth_range = NdcDepthRange::ZeroToOne;
    params.near_plane = 0.1F;
    params.far_plane = 1000.0F;
    return ResolvedView(params);
  }

  Vec3 cam_pos { 0.0F, 0.0F, 0.0F };
  Quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };
  if (auto wp = camera_node.GetTransform().GetWorldPosition()) {
    cam_pos = *wp;
  } else if (auto lp = camera_node.GetTransform().GetLocalPosition()) {
    cam_pos = *lp;
  }
  if (auto wr = camera_node.GetTransform().GetWorldRotation()) {
    cam_rot = *wr;
  } else if (auto lr = camera_node.GetTransform().GetLocalRotation()) {
    cam_rot = *lr;
  }

  const auto view_m = [](const Vec3& pos, const Quat& rot) -> Mat4 {
    const Vec3 forward = rot * space::look::Forward;
    const Vec3 up = rot * space::look::Up;
    return glm::lookAt(pos, pos + forward, up);
  }(cam_pos, cam_rot);

  Mat4 proj_m { 1.0F };
  float near_plane = 0.1F;
  float far_plane = 1000.0F;
  std::optional<float> camera_ev {};
  NdcDepthRange src_range = NdcDepthRange::ZeroToOne;
  if (auto cam = camera_node.GetCameraAs<scene::PerspectiveCamera>()) {
    proj_m = cam->get().ProjectionMatrix();
    near_plane = cam->get().GetNearPlane();
    far_plane = cam->get().GetFarPlane();
    camera_ev = cam->get().Exposure().GetEv();
  } else if (auto camo = camera_node.GetCameraAs<scene::OrthographicCamera>()) {
    proj_m = camo->get().ProjectionMatrix();
    const auto ext = camo->get().GetExtents();
    near_plane = ext[4];
    far_plane = ext[5];
    camera_ev = camo->get().Exposure().GetEv();
  }

  View cfg;
  cfg.reverse_z = true;
  if (auto cam = camera_node.GetCameraAs<scene::PerspectiveCamera>()) {
    cfg.viewport = cam->get().ActiveViewport();
  } else if (auto camo = camera_node.GetCameraAs<scene::OrthographicCamera>()) {
    cfg.viewport = camo->get().ActiveViewport();
  }
  if (viewport_override.has_value() && viewport_override->IsValid()) {
    cfg.viewport = *viewport_override;
  }

  const auto stable_proj_m
    = RemapProjectionDepthRange(proj_m, src_range, NdcDepthRange::ZeroToOne);

  proj_m = ApplyJitterToProjection(proj_m, cfg.pixel_jitter, cfg.viewport);
  proj_m
    = RemapProjectionDepthRange(proj_m, src_range, NdcDepthRange::ZeroToOne);

  ResolvedView::Params params;
  params.view_config = cfg;
  params.view_matrix = view_m;
  params.proj_matrix = proj_m;
  params.stable_proj_matrix = stable_proj_m;
  params.depth_range = NdcDepthRange::ZeroToOne;
  params.camera_position = cam_pos;
  params.camera_ev = camera_ev;
  params.near_plane = near_plane;
  params.far_plane = far_plane;
  return ResolvedView(params);
}

} // namespace

auto access::ViewLifecycleTagFactory::Get() noexcept -> ViewLifecycleAccessTag
{
  return ViewLifecycleAccessTag {};
}

struct ViewLifecycleService::State {
  std::map<ViewId, CompositionViewImpl> view_pool;
  std::vector<CompositionViewImpl*> sorted_views;
};

ViewLifecycleService::ViewLifecycleService(
  UpsertPublishedViewCallback upsert_published_view,
  ResolvePublishedViewCallback resolve_published_view,
  PruneStalePublishedViewsCallback prune_stale_published_views,
  RegisterViewGraphCallback register_view_graph,
  RenderViewCoroutine render_view_coroutine)
  : upsert_published_view_(std::move(upsert_published_view))
  , resolve_published_view_(std::move(resolve_published_view))
  , prune_stale_published_views_(std::move(prune_stale_published_views))
  , register_view_graph_(std::move(register_view_graph))
  , render_view_coroutine_(std::move(render_view_coroutine))
  , state_(std::make_unique<State>())
{
  CHECK_F(static_cast<bool>(upsert_published_view_),
    "ViewLifecycleService requires an upsert_published_view callback");
  CHECK_F(static_cast<bool>(resolve_published_view_),
    "ViewLifecycleService requires a resolve_published_view callback");
  CHECK_F(static_cast<bool>(prune_stale_published_views_),
    "ViewLifecycleService requires a prune_stale_published_views callback");
  CHECK_F(static_cast<bool>(register_view_graph_),
    "ViewLifecycleService requires a register_view_graph callback");
  CHECK_F(static_cast<bool>(render_view_coroutine_),
    "ViewLifecycleService requires a render_view_coroutine callback");
}

ViewLifecycleService::~ViewLifecycleService() = default;

void ViewLifecycleService::SyncActiveViews(engine::FrameContext& /*context*/,
  std::span<const CompositionView> view_descs,
  observer_ptr<graphics::Framebuffer> composite_target, Graphics& graphics)
{
  state_->sorted_views.clear();
  state_->sorted_views.reserve(view_descs.size());

  uint32_t index = 0;
  for (auto desc : view_descs) {
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
    view_impl.PrepareForRender(
      desc, index++, graphics, access::ViewLifecycleTagFactory::Get());
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
  const auto published_view_id
    = resolve_published_view_(view.GetDescriptor().id);
  CHECK_F(published_view_id != kInvalidViewId,
    "RegisterViewRenderGraph called for unpublished view '{}'",
    view.GetDescriptor().name);
  auto camera = view.GetDescriptor().camera.value_or(scene::SceneNode {});
  register_view_graph_(published_view_id, render_view_coroutine_,
    ResolveViewForCameraNode(camera, view.GetDescriptor().view.viewport));
}

void ViewLifecycleService::PublishViews(engine::FrameContext& context)
{
  const auto build_view_context
    = [](const CompositionViewImpl& view,
        const ViewId exposure_view_id) -> engine::ViewContext {
    engine::ViewContext view_ctx;
    view_ctx.view = view.GetDescriptor().view;
    const bool has_scene = view.GetDescriptor().camera.has_value();
    view_ctx.metadata = { .name = std::string(view.GetDescriptor().name),
      .purpose = has_scene ? "scene" : "overlay",
      .is_scene_view = has_scene,
      .with_atmosphere = view.GetDescriptor().with_atmosphere,
      .exposure_view_id = exposure_view_id };
    view_ctx.render_target = view.GetHdrFramebuffer()
      ? observer_ptr { view.GetHdrFramebuffer().get() }
      : observer_ptr { view.GetSdrFramebuffer().get() };
    view_ctx.composite_source = view.GetSdrFramebuffer()
      ? observer_ptr { view.GetSdrFramebuffer().get() }
      : view_ctx.render_target;

    CHECK_F(!has_scene || view.GetDescriptor().enable_hdr,
      "Scene view '{}' must enable HDR rendering", view.GetDescriptor().name);
    CHECK_NOTNULL_F(view_ctx.render_target.get(),
      "View '{}' missing render_target framebuffer", view.GetDescriptor().name);
    CHECK_NOTNULL_F(view_ctx.composite_source.get(),
      "View '{}' missing composite_source framebuffer",
      view.GetDescriptor().name);
    if (has_scene) {
      CHECK_NOTNULL_F(view.GetHdrFramebuffer().get(),
        "Scene view '{}' missing HDR framebuffer", view.GetDescriptor().name);
      CHECK_NOTNULL_F(view.GetSdrFramebuffer().get(),
        "Scene view '{}' missing SDR framebuffer", view.GetDescriptor().name);
    }
    return view_ctx;
  };

  for (auto* view : state_->sorted_views) {
    auto view_ctx = build_view_context(*view, kInvalidViewId);
    const auto previous_published_view_id
      = resolve_published_view_(view->GetDescriptor().id);
    const auto published_view_id = upsert_published_view_(
      context, view->GetDescriptor().id, std::move(view_ctx));
    if (previous_published_view_id == kInvalidViewId) {
      LOG_F(INFO,
        "Registered View '{}' (IntentID: {}) with Engine "
        "(PublishedViewId: {})",
        view->GetDescriptor().name, view->GetDescriptor().id.get(),
        published_view_id.get());
    } else {
      DLOG_F(1, "Updated View '{}' (PublishedViewId: {})",
        view->GetDescriptor().name, published_view_id.get());
    }
  }

  std::unordered_map<ViewId, std::size_t> sorted_indices;
  sorted_indices.reserve(state_->sorted_views.size());
  for (std::size_t i = 0; i < state_->sorted_views.size(); ++i) {
    sorted_indices.emplace(state_->sorted_views[i]->GetDescriptor().id, i);
  }

  for (auto* view : state_->sorted_views) {
    const auto self_published_view_id
      = resolve_published_view_(view->GetDescriptor().id);
    CHECK_F(self_published_view_id != kInvalidViewId,
      "View '{}' must be published before exposure resolution",
      view->GetDescriptor().name);

    ViewId resolved_exposure_view_id = self_published_view_id;
    if (const auto requested_source_id
      = view->GetDescriptor().exposure_source_view_id;
      requested_source_id != kInvalidViewId
      && requested_source_id != view->GetDescriptor().id) {
      const auto source_it = state_->view_pool.find(requested_source_id);
      CHECK_F(source_it != state_->view_pool.end(),
        "View '{}' references missing exposure source intent id {}",
        view->GetDescriptor().name, requested_source_id.get());

      const auto source_index_it = sorted_indices.find(requested_source_id);
      CHECK_F(source_index_it != sorted_indices.end(),
        "View '{}' references inactive exposure source intent id {}",
        view->GetDescriptor().name, requested_source_id.get());

      const auto consumer_index = sorted_indices.at(view->GetDescriptor().id);
      CHECK_F(source_index_it->second <= consumer_index,
        "View '{}' references exposure source '{}' that renders later in the "
        "frame",
        view->GetDescriptor().name, source_it->second.GetDescriptor().name);

      resolved_exposure_view_id = resolve_published_view_(requested_source_id);
      CHECK_F(resolved_exposure_view_id != kInvalidViewId,
        "View '{}' references exposure source '{}' before publication",
        view->GetDescriptor().name, source_it->second.GetDescriptor().name);
    }

    auto resolved_view_ctx
      = build_view_context(*view, resolved_exposure_view_id);
    upsert_published_view_(
      context, view->GetDescriptor().id, std::move(resolved_view_ctx));
  }
}

void ViewLifecycleService::RegisterRenderGraphs()
{
  for (auto* view : state_->sorted_views) {
    RegisterViewRenderGraph(*view);
  }
}

void ViewLifecycleService::UnpublishStaleViews(engine::FrameContext& context)
{
  const auto stale_view_ids = prune_stale_published_views_(context);
  for (const auto stale_view_id : stale_view_ids) {
    if (const auto it = state_->view_pool.find(stale_view_id);
      it != state_->view_pool.end()) {
      LOG_F(INFO, "Reaping View resources for ID {}", stale_view_id);
      state_->view_pool.erase(it);
    }
  }
}

auto ViewLifecycleService::GetOrderedActiveViews() const
  -> const std::vector<CompositionViewImpl*>&
{
  return state_->sorted_views;
}

} // namespace oxygen::vortex::internal
