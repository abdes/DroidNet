//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/EnumIndexedArray.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/ImGui/api_export.h>

namespace oxygen::engine {
class FrameContext;
} // namespace oxygen::engine

namespace oxygen::imgui {

//! Which frame statistics the overlay shows; all off draws nothing.
struct StatsOverlayConfig {
  //! Frames per second and the average frame time.
  bool show_fps { false };
  //! The time of every engine phase that does work, slowest three
  //! highlighted.
  bool show_frame_timing_detail { false };
  //! Engine CPU time of the last frame and the time spent pacing.
  bool show_engine_timing { false };
  //! Engine CPU time against the CPU budget, and the GPU budget.
  bool show_budget_stats { false };

  [[nodiscard]] constexpr auto ShowsAnything() const noexcept -> bool
  {
    return show_fps || show_frame_timing_detail || show_engine_timing
      || show_budget_stats;
  }
};

//! Frame statistics in a small translucent panel at the top-right of the
//! display, headed by the frame sequence number so a screenshot of a defect
//! names its frame.
/*!
 Values are averaged over about half a second so they can be read while the
 frame time varies; the overlay keeps that average between frames, so use
 one instance per display. It takes no input.
*/
class StatsOverlay final {
public:
  //! Draws the panel; `top_margin` keeps it below a toolbar, in pixels.
  OXGN_IMGUI_API auto Draw(const engine::FrameContext& context,
    const StatsOverlayConfig& config, float top_margin = 0.0F) -> void;

private:
  auto Sample(const engine::FrameContext& context) -> void;

  bool primed_ { false };
  float fps_ { 0.0F };
  float frame_ms_ { 0.0F };
  float engine_ms_ { 0.0F };
  float pacing_ms_ { 0.0F };
  EnumIndexedArray<core::PhaseId, float> phase_ms_ {};
};

} // namespace oxygen::imgui
