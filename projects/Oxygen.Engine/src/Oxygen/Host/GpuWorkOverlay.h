//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/api_export.h>
#include <Oxygen/ImGui/Widgets/GpuWorkChart.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>

namespace oxygen {
namespace console {
  class Console;
} // namespace console
namespace vortex {
  class Renderer;
} // namespace vortex
} // namespace oxygen

namespace oxygen::host {

//! Shows the GPU work overlay; F1 hides it with the stats overlay.
inline constexpr std::string_view kGpuWorkCVar = "host.ui.gpu_work";

//! Overlay position in pixels from the top-left of the window's work area.
using GpuWorkPosition = std::array<float, 2>;

//! The renderer's GPU work summary as a chart, shown below the toolbar until
//! it is dragged elsewhere by any part that is not a control.
/*!
 Rows: the whole frame with the main view's groups, the other views, shared
 work (done once for all views) and GPU idle as segments; one collapsed row
 per further view; shared work; then the frame sparkline and the valid and
 invalid frame counts. Rows are rebuilt only when the summary's
 revision changes.

 The overlay holds a GPU timeline lease only while it is shown, so a hidden
 overlay costs nothing. Where it was dropped is application UI state: an
 application that keeps it reads `Position()` and restores it with
 `RestorePosition()`.
*/
class GpuWorkOverlay final {
public:
  OXGN_HOST_API static auto RegisterConsoleBindings(console::Console& console)
    -> void;

  //! Draws the overlay when `shown`, else releases the lease. Call inside an
  //! ImGui frame. A null renderer draws nothing.
  OXGN_HOST_API auto Draw(observer_ptr<vortex::Renderer> renderer, bool shown,
    std::optional<ViewId> main_view, std::chrono::milliseconds budget,
    float top_margin) -> void;

  //! Where the overlay was last dropped; none until it is first moved.
  [[nodiscard]] auto Position() const noexcept -> std::optional<GpuWorkPosition>
  {
    return dropped_;
  }

  //! Places the overlay at `position` when it is next drawn; a position
  //! beyond a smaller window is pulled back within reach.
  OXGN_HOST_API auto RestorePosition(GpuWorkPosition position) -> void;

  //! Releases the lease; call before the renderer goes away.
  OXGN_HOST_API auto Release() noexcept -> void;

private:
  auto Rebuild(const vortex::GpuWorkSummary& summary,
    std::optional<ViewId> main_view) -> void;

  //! Records where a drag ended; call between Begin and End.
  auto TrackPosition() -> void;

  vortex::GpuTimelineLease lease_;
  //! The window position seen last, to tell a drag from a placement.
  std::optional<GpuWorkPosition> seen_;
  std::optional<GpuWorkPosition> dropped_;
  std::optional<GpuWorkPosition> restore_;
  observer_ptr<vortex::Renderer> leased_renderer_ { nullptr };
  std::uint64_t revision_ { 0U };
  imgui::GpuWorkChartData chart_;
};

} // namespace oxygen::host
