//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/ImGui/GpuTimelinePresentationSmoother.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::internal {
class GpuTimelineProfiler;
}

namespace oxygen::engine::imgui {

class GpuTimelinePanel final {
public:
  OXGN_RNDR_API explicit GpuTimelinePanel(
    observer_ptr<internal::GpuTimelineProfiler> profiler);

  OXYGEN_MAKE_NON_COPYABLE(GpuTimelinePanel)
  OXYGEN_MAKE_NON_MOVABLE(GpuTimelinePanel)

  OXGN_RNDR_API ~GpuTimelinePanel() = default;

  OXGN_RNDR_API auto SetVisible(bool visible) -> void;
  [[nodiscard]] OXGN_RNDR_API auto IsVisible() const noexcept -> bool;
  OXGN_RNDR_API auto Draw() -> void;

private:
  observer_ptr<internal::GpuTimelineProfiler> profiler_ { nullptr };
  GpuTimelinePresentationSmoother presentation_smoother_ {};
  bool show_pass_phase_details_ { false };
  bool visible_ { false };
};

} // namespace oxygen::engine::imgui
