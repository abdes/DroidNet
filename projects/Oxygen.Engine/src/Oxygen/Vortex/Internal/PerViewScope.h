//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Vortex/RenderContext.h>

namespace oxygen::vortex::internal {

class PerViewScope {
public:
  PerViewScope(RenderContext& ctx, const std::size_t view_index)
    : ctx_(ctx)
    , saved_current_view_(ctx.current_view)
    , saved_active_view_index_(ctx.active_view_index)
    , saved_pass_target_(ctx.pass_target)
    , saved_render_mode_(ctx.render_mode)
  {
    CHECK_LT_F(view_index, ctx.frame_views.size(),
      "PerViewScope requires a valid frame view index");
    CHECK_F(!ctx.per_view_scope_active_,
      "Nested PerViewScope on the same RenderContext is not allowed");

    ctx.per_view_scope_active_ = true;
    ctx.active_view_index = view_index;

    const auto& entry = ctx.frame_views[view_index];
    ctx.current_view = RenderContext::ViewSpecific {
      .view_id = entry.view_id,
      .exposure_view_id = entry.exposure_view_id,
      .view_state_handle = entry.view_state_handle,
      .exposure_view_state_handle = entry.exposure_view_state_handle,
      .composition_view = entry.composition_view,
      .feature_profile = entry.feature_profile,
      .feature_mask = entry.feature_mask,
      .shading_mode_override = entry.shading_mode_override,
      .resolved_view = entry.resolved_view,
      .is_reflection_capture = entry.is_reflection_capture,
      .with_atmosphere = entry.with_atmosphere,
      .with_height_fog = entry.with_height_fog,
      .with_local_fog = entry.with_local_fog,
    };
    if (entry.render_mode_override.has_value()) {
      ctx.render_mode = entry.render_mode_override.value();
    }
    ctx.pass_target = entry.primary_target;
  }

  ~PerViewScope()
  {
    ctx_.current_view = saved_current_view_;
    ctx_.active_view_index = saved_active_view_index_;
    ctx_.pass_target = saved_pass_target_;
    ctx_.render_mode = saved_render_mode_;
    ctx_.per_view_scope_active_ = false;
  }

  OXYGEN_MAKE_NON_COPYABLE(PerViewScope)
  OXYGEN_MAKE_NON_MOVABLE(PerViewScope)

private:
  RenderContext& ctx_;
  RenderContext::ViewSpecific saved_current_view_;
  std::size_t saved_active_view_index_;
  observer_ptr<const graphics::Framebuffer> saved_pass_target_;
  RenderMode saved_render_mode_;
};

} // namespace oxygen::vortex::internal
