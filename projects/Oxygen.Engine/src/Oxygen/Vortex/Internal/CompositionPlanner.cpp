//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/Internal/CompositionPlanner.h>
#include <Oxygen/Vortex/Internal/CompositionViewImpl.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h>

namespace oxygen::vortex::internal {

namespace {

  constexpr auto kViewportEpsilon = 0.5F;

  auto ResolveRouteDestination(
    const CompositionView::ViewSurfaceRoute& route, const FrameViewPacket& packet)
    -> ViewPort
  {
    if (route.destination.IsValid()) {
      return route.destination;
    }
    return packet.GetCompositeViewport();
  }

  auto ViewportCoversTarget(
    const ViewPort& viewport, const graphics::Texture& target) -> bool
  {
    const auto& desc = target.GetDescriptor();
    return std::abs(viewport.top_left_x) <= kViewportEpsilon
      && std::abs(viewport.top_left_y) <= kViewportEpsilon
      && std::abs(viewport.width - static_cast<float>(desc.width))
        <= kViewportEpsilon
      && std::abs(viewport.height - static_cast<float>(desc.height))
        <= kViewportEpsilon;
  }

} // namespace

CompositionPlanner::~CompositionPlanner() = default;

void CompositionPlanner::PlanCompositingTasks()
{
  DCHECK_NOTNULL_F(frame_plan_builder_.get());
  planned_layers_.clear();
  planned_surface_overlays_.clear();
  const auto& frame_view_packets = frame_plan_builder_->GetFrameViewPackets();
  planned_layers_.reserve(frame_view_packets.size());
  for (const auto& packet : frame_view_packets) {
    const auto& desc = packet.View().GetDescriptor();
    const auto append_surface_overlay
      = [this, &packet,
          &desc](const CompositionView::ViewSurfaceRoute& route,
          const CompositionView::OverlayBatch& overlay) {
          if (overlay.lane != CompositionView::OverlayLane::kViewScreen
            && overlay.lane != CompositionView::OverlayLane::kSurfaceScreen) {
            return;
          }
          auto batch = overlay;
          batch.target = CompositionView::OverlayTarget::kSurface;
          batch.surface_id = route.surface_id;
          if (batch.view_id == kInvalidViewId) {
            batch.view_id = packet.PublishedViewId();
          }
          if (batch.debug_name.empty()) {
            batch.debug_name = fmt::format("Vortex.Surface[{}].Overlay[{}:{}]",
              route.surface_id.get(), desc.name, packet.PublishedViewId().get());
          }
          planned_surface_overlays_.push_back(SurfaceOverlayPlan {
            .surface_id = route.surface_id,
            .batch = std::move(batch),
            .z_order = desc.z_order,
            .submission_order = packet.View().GetSubmissionOrder(),
          });
        };
    const auto append_surface_overlays
      = [&packet, &append_surface_overlay](
          const CompositionView::ViewSurfaceRoute& route) {
          for (const auto& overlay : packet.OverlayBatches()) {
            append_surface_overlay(route, overlay);
          }
        };

    if (packet.SurfaceRoutes().empty()) {
      append_surface_overlays(CompositionView::ViewSurfaceRoute {
        .surface_id = CompositionView::kDefaultSurfaceRoute,
        .destination = packet.GetCompositeViewport(),
        .blend_mode = CompositionView::SurfaceRouteBlendMode::kAlphaBlend,
      });
    } else {
      for (const auto& route : packet.SurfaceRoutes()) {
        append_surface_overlays(route);
      }
    }

    if (!packet.HasCompositeTexture()) {
      continue;
    }
    const auto append_layer
      = [this, &packet, &desc](const CompositionView::ViewSurfaceRoute& route) {
          const auto surface_id = route.surface_id;
          const auto layer_index = planned_layers_.size();
          planned_layers_.push_back(CompositionLayerPlan {
            .surface_id = surface_id,
            .source_view_id = packet.PublishedViewId(),
            .source_texture = packet.GetCompositeTexture(),
            .destination = ResolveRouteDestination(route, packet),
            .blend_mode = route.blend_mode,
            .z_order = desc.z_order,
            .submission_order = packet.View().GetSubmissionOrder(),
            .opacity = packet.GetCompositeOpacity(),
            .debug_name = fmt::format("Vortex.Surface[{}].Layer[{}:{}:{}]",
              surface_id.get(), desc.name, packet.PublishedViewId().get(),
              layer_index),
          });
        };

    if (packet.SurfaceRoutes().empty()) {
      append_layer(CompositionView::ViewSurfaceRoute {
        .surface_id = CompositionView::kDefaultSurfaceRoute,
        .destination = packet.GetCompositeViewport(),
        .blend_mode = CompositionView::SurfaceRouteBlendMode::kAlphaBlend,
      });
    } else {
      for (const auto& route : packet.SurfaceRoutes()) {
        append_layer(route);
      }
    }
  }

  std::ranges::stable_sort(planned_layers_,
    [](const CompositionLayerPlan& lhs, const CompositionLayerPlan& rhs) {
      if (lhs.surface_id != rhs.surface_id) {
        return lhs.surface_id.get() < rhs.surface_id.get();
      }
      if (lhs.z_order.get() != rhs.z_order.get()) {
        return lhs.z_order.get() < rhs.z_order.get();
      }
      return lhs.submission_order < rhs.submission_order;
    });
  std::ranges::stable_sort(planned_surface_overlays_,
    [](const SurfaceOverlayPlan& lhs, const SurfaceOverlayPlan& rhs) {
      if (lhs.surface_id != rhs.surface_id) {
        return lhs.surface_id.get() < rhs.surface_id.get();
      }
      if (lhs.z_order.get() != rhs.z_order.get()) {
        return lhs.z_order.get() < rhs.z_order.get();
      }
      if (lhs.batch.priority != rhs.batch.priority) {
        return lhs.batch.priority < rhs.batch.priority;
      }
      return lhs.submission_order < rhs.submission_order;
    });
}

auto CompositionPlanner::BuildCompositionSubmission(
  std::shared_ptr<graphics::Framebuffer> final_output,
  const CompositionView::SurfaceRouteId surface_id)
  -> oxygen::vortex::CompositionSubmission
{
  if (!final_output) {
    LOG_F(WARNING, "Vortex: skipping compositing because target is null");
    return {};
  }

  const auto& target_desc = final_output->GetDescriptor();
  if (target_desc.color_attachments.empty()
    || !target_desc.color_attachments[0].texture) {
    LOG_F(WARNING,
      "Vortex: skipping compositing because composite_target has no color "
      "attachment texture");
    return {};
  }

  oxygen::vortex::CompositionSubmission submission;
  submission.surface_id = surface_id;
  submission.debug_name = fmt::format("Vortex.Surface[{}].Composite",
    surface_id.get());
  submission.composite_target = std::move(final_output);
  submission.tasks.reserve(planned_layers_.size());

  auto& target = *target_desc.color_attachments[0].texture;
  const auto can_fast_copy_layer = [&target](const auto& layer) {
    return layer.opacity >= 1.0F && layer.source_texture
      && layer.source_texture->GetDescriptor().format
        == target.GetDescriptor().format
      && ViewportCoversTarget(layer.destination, target);
  };
  for (const auto& layer : planned_layers_) {
    if (layer.surface_id != surface_id || layer.opacity <= 0.0F) {
      continue;
    }

    if (layer.blend_mode == CompositionView::SurfaceRouteBlendMode::kCopy
      || can_fast_copy_layer(layer)) {
      submission.tasks.push_back(CompositingTask::MakeCopy(
        layer.source_view_id, layer.destination, layer.debug_name));
      continue;
    }

    submission.tasks.push_back(CompositingTask::MakeTextureBlend(
      layer.source_texture, layer.destination, layer.opacity,
      layer.debug_name));
  }
  for (const auto& overlay : planned_surface_overlays_) {
    if (overlay.surface_id == surface_id) {
      submission.surface_overlays.push_back(overlay.batch);
    }
  }
  return submission;
}

} // namespace oxygen::vortex::internal
