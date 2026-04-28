//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/Internal/CompositionViewImpl.h>
#include <Oxygen/Vortex/Internal/FrameViewPacket.h>

namespace oxygen::vortex::internal {

FrameViewPacket::FrameViewPacket(observer_ptr<const CompositionViewImpl> view,
  ViewId published_view_id, CompositionView::ViewStateHandle view_state_handle,
  ViewRenderPlan plan)
  : view_(view)
  , published_view_id_(published_view_id)
  , view_state_handle_(view_state_handle)
  , plan_(plan)
{
  CHECK_NOTNULL_F(view_.get(), "FrameViewPacket requires non-null view");
  CHECK_F(published_view_id_ != kInvalidViewId,
    "FrameViewPacket requires a published runtime view id");

  const auto& descriptor = view_->GetDescriptor();
  view_kind_ = descriptor.view_kind;
  feature_mask_ = descriptor.feature_mask;
  surface_routes_ = descriptor.surface_routes;
  overlay_policy_ = descriptor.overlay_policy;
  overlay_batches_ = descriptor.overlay_batches;
  if (descriptor.on_overlay) {
    if (!std::ranges::contains(
          overlay_policy_.lanes, CompositionView::OverlayLane::kViewScreen)) {
      overlay_policy_.lanes.push_back(CompositionView::OverlayLane::kViewScreen);
    }
    overlay_batches_.push_back(CompositionView::OverlayBatch {
      .lane = CompositionView::OverlayLane::kViewScreen,
      .target = CompositionView::OverlayTarget::kView,
      .view_id = published_view_id_,
      .surface_id = CompositionView::kDefaultSurfaceRoute,
      .priority = descriptor.z_order.get(),
      .debug_name = fmt::format("Vortex.View[{}:{}].Overlay[ViewScreen]",
        descriptor.name, published_view_id_.get()),
      .record = descriptor.on_overlay,
    });
  }
  for (const auto lane : overlay_policy_.lanes) {
    if (lane != CompositionView::OverlayLane::kWorldDepthAware
      && lane != CompositionView::OverlayLane::kWorldForeground) {
      continue;
    }
    const auto exists = std::ranges::any_of(overlay_batches_,
      [lane](const CompositionView::OverlayBatch& batch) {
        return batch.lane == lane;
      });
    if (exists) {
      continue;
    }
    overlay_batches_.push_back(CompositionView::OverlayBatch {
      .lane = lane,
      .target = CompositionView::OverlayTarget::kView,
      .view_id = published_view_id_,
      .surface_id = CompositionView::kDefaultSurfaceRoute,
      .priority = descriptor.z_order.get(),
      .debug_name = fmt::format("Vortex.View[{}:{}].Overlay[{}]",
        descriptor.name, published_view_id_.get(),
        lane == CompositionView::OverlayLane::kWorldDepthAware
          ? "WorldDepthAware"
          : "WorldForeground"),
      .record = {},
    });
  }
  produced_aux_outputs_ = descriptor.produced_aux_outputs;
  consumed_aux_outputs_ = descriptor.consumed_aux_outputs;
}

auto FrameViewPacket::HasCompositeTexture() const noexcept -> bool
{
  return View().GetSdrTexture() != nullptr;
}

auto FrameViewPacket::GetCompositeTexture() const
  -> std::shared_ptr<graphics::Texture>
{
  return View().GetSdrTexture();
}

auto FrameViewPacket::GetCompositeViewport() const noexcept -> ViewPort
{
  return View().GetDescriptor().view.viewport;
}

auto FrameViewPacket::GetCompositeOpacity() const noexcept -> float
{
  return View().GetDescriptor().opacity;
}

} // namespace oxygen::vortex::internal
