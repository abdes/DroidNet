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
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Types/CompositingTask.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::internal {

class FramePlanBuilder;

class CompositionPlanner {
public:
  explicit CompositionPlanner(observer_ptr<FramePlanBuilder> frame_plan_builder)
    : frame_plan_builder_(frame_plan_builder)
  {
  }
  OXGN_VRTX_API ~CompositionPlanner();
  OXYGEN_MAKE_NON_COPYABLE(CompositionPlanner)
  OXYGEN_MAKE_NON_MOVABLE(CompositionPlanner)

  OXGN_VRTX_API void PlanCompositingTasks();
  OXGN_VRTX_API auto BuildCompositionSubmission(
    std::shared_ptr<graphics::Framebuffer> final_output,
    CompositionView::SurfaceRouteId surface_id
    = CompositionView::kDefaultSurfaceRoute)
    -> oxygen::vortex::CompositionSubmission;

private:
  struct CompositionLayerPlan {
    CompositionView::SurfaceRouteId surface_id {
      CompositionView::kDefaultSurfaceRoute
    };
    ViewId source_view_id { kInvalidViewId };
    std::shared_ptr<graphics::Texture> source_texture {};
    ViewPort destination {};
    CompositionView::SurfaceRouteBlendMode blend_mode {
      CompositionView::SurfaceRouteBlendMode::kAlphaBlend
    };
    CompositionView::ZOrder z_order {};
    std::uint32_t submission_order { 0U };
    float opacity { 1.0F };
    std::string debug_name {};
  };

  observer_ptr<FramePlanBuilder> frame_plan_builder_;
  std::vector<CompositionLayerPlan> planned_layers_;
};

} // namespace oxygen::vortex::internal
