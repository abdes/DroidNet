//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/ViewRenderPlan.h>

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex::internal {

class CompositionViewImpl;

struct AuxiliaryResolvedInput {
  CompositionView::AuxInputDesc input {};
  CompositionView::AuxOutputKind kind {
    CompositionView::AuxOutputKind::kColorTexture
  };
  ViewId producer_view_id { kInvalidViewId };
  std::uint32_t producer_packet_index { 0U };
  bool valid { false };
  std::string debug_name {};
};

class FrameViewPacket {
public:
  FrameViewPacket(observer_ptr<const CompositionViewImpl> view,
    ViewId published_view_id, CompositionView::ViewStateHandle view_state_handle,
    ViewRenderPlan plan);
  ~FrameViewPacket() = default;
  OXYGEN_DEFAULT_COPYABLE(FrameViewPacket)
  OXYGEN_DEFAULT_MOVABLE(FrameViewPacket)

  [[nodiscard]] auto View() const noexcept -> const CompositionViewImpl&
  {
    return *view_;
  }

  [[nodiscard]] auto Plan() const noexcept -> const ViewRenderPlan&
  {
    return plan_;
  }
  [[nodiscard]] auto PublishedViewId() const noexcept -> ViewId
  {
    return published_view_id_;
  }
  [[nodiscard]] auto ViewStateHandle() const noexcept
    -> CompositionView::ViewStateHandle
  {
    return view_state_handle_;
  }
  [[nodiscard]] auto Kind() const noexcept -> CompositionView::ViewKind
  {
    return view_kind_;
  }
  [[nodiscard]] auto FeatureMask() const noexcept
    -> const CompositionView::ViewFeatureMask&
  {
    return feature_mask_;
  }
  [[nodiscard]] auto SurfaceRoutes() const noexcept
    -> const std::vector<CompositionView::ViewSurfaceRoute>&
  {
    return surface_routes_;
  }
  [[nodiscard]] auto GetOverlayPolicy() const noexcept
    -> const CompositionView::OverlayPolicy&
  {
    return overlay_policy_;
  }
  [[nodiscard]] auto OverlayBatches() const noexcept
    -> const std::vector<CompositionView::OverlayBatch>&
  {
    return overlay_batches_;
  }
  [[nodiscard]] auto ProducedAuxOutputs() const noexcept
    -> const std::vector<CompositionView::AuxOutputDesc>&
  {
    return produced_aux_outputs_;
  }
  [[nodiscard]] auto ConsumedAuxOutputs() const noexcept
    -> const std::vector<CompositionView::AuxInputDesc>&
  {
    return consumed_aux_outputs_;
  }
  [[nodiscard]] auto ResolvedAuxInputs() const noexcept
    -> const std::vector<AuxiliaryResolvedInput>&
  {
    return resolved_aux_inputs_;
  }
  auto SetResolvedAuxInputs(
    std::vector<AuxiliaryResolvedInput> resolved_inputs) noexcept -> void
  {
    resolved_aux_inputs_ = std::move(resolved_inputs);
  }

  [[nodiscard]] auto HasCompositeTexture() const noexcept -> bool;
  [[nodiscard]] auto GetCompositeTexture() const
    -> std::shared_ptr<graphics::Texture>;
  [[nodiscard]] auto GetCompositeViewport() const noexcept -> ViewPort;
  [[nodiscard]] auto GetCompositeOpacity() const noexcept -> float;

private:
  observer_ptr<const CompositionViewImpl> view_;
  ViewId published_view_id_ { kInvalidViewId };
  CompositionView::ViewStateHandle view_state_handle_ {
    CompositionView::kInvalidViewStateHandle
  };
  CompositionView::ViewKind view_kind_ { CompositionView::ViewKind::kPrimary };
  CompositionView::ViewFeatureMask feature_mask_ {};
  std::vector<CompositionView::ViewSurfaceRoute> surface_routes_ {};
  CompositionView::OverlayPolicy overlay_policy_ {};
  std::vector<CompositionView::OverlayBatch> overlay_batches_ {};
  std::vector<CompositionView::AuxOutputDesc> produced_aux_outputs_ {};
  std::vector<CompositionView::AuxInputDesc> consumed_aux_outputs_ {};
  std::vector<AuxiliaryResolvedInput> resolved_aux_inputs_ {};
  ViewRenderPlan plan_;
};

} // namespace oxygen::vortex::internal
