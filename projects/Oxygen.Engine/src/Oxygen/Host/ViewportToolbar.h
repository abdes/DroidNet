//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <optional>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/GpuWorkOverlay.h>
#include <Oxygen/Host/api_export.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>
#include <Oxygen/ImGui/Widgets/StatsOverlay.h>
#include <Oxygen/ImGui/Widgets/Toolbar.h>

namespace oxygen {
class IAsyncEngine;
namespace console {
  class Console;
} // namespace console
namespace engine {
  class FrameContext;
} // namespace engine
namespace imgui::consoleui {
  class ConsoleUiState;
} // namespace imgui::consoleui
namespace vortex {
  class Renderer;
} // namespace vortex
} // namespace oxygen

namespace oxygen::host {

//! Shows the toolbar, the stats overlay and the GPU work overlay; F1 toggles
//! it.
inline constexpr std::string_view kUiVisibleCVar = "host.ui.visible";

struct StatsCVarInfo {
  std::string_view cvar;
  std::string_view label;
};

//! The stats overlay sections, one variable each, in menu order.
inline constexpr auto kStatsCVars = std::array {
  StatsCVarInfo { .cvar = "host.ui.stats.fps", .label = "FPS" },
  StatsCVarInfo { .cvar = "host.ui.stats.engine", .label = "Engine Timing" },
  StatsCVarInfo { .cvar = "host.ui.stats.phases", .label = "Phase Timings" },
  StatsCVarInfo { .cvar = "host.ui.stats.budget", .label = "Budget" },
};

//! The strip across the top of the host window: view mode, show flags,
//! stats, play controls and the console button.
/*!
 Every control is a shortcut to a console variable or command, and shows
 the variable's current value, so a change typed in the console appears at
 once. The play controls read the engine's session and pause state each
 frame; Play and Stop appear only when the application registers
 `ngin.session.begin`.

 A scene view whose rendering failed is named in a banner under the strip,
 also while the toolbar is hidden.

 Shortcuts, ignored while a text field has focus: F1 toggles
 `host.ui.visible`; Alt+4, Alt+3 and Alt+2 select lit, unlit and wireframe.
*/
class ViewportToolbar final {
public:
  //! Registers `host.ui.visible`, the `host.ui.stats.*` variables and
  //! `host.ui.gpu_work`.
  OXGN_HOST_API static auto RegisterConsoleBindings(console::Console& console)
    -> void;

  //! Draws the toolbar and the overlays. Call inside an ImGui frame.
  /*!
   `renderer` may be null; the view-mode control then cannot label a
   renderer-global mode and the GPU work overlay draws nothing. The GPU
   work overlay names `main_view` as the frame's main view.
  */
  OXGN_HOST_API auto Draw(IAsyncEngine& engine,
    const engine::FrameContext& context,
    observer_ptr<vortex::Renderer> renderer, std::optional<ViewId> main_view,
    imgui::consoleui::ConsoleUiState& console_ui,
    const imgui::consoleui::LineRunner& run) -> void;

  //! Releases the GPU work overlay's timeline lease; call before the
  //! renderer goes away.
  OXGN_HOST_API auto ReleaseGpuTimeline() noexcept -> void;

  //! Height of the strip drawn this frame; zero while the toolbar is hidden.
  //! The GPU work overlay, whose position an application may keep.
  [[nodiscard]] auto GpuWork() noexcept -> GpuWorkOverlay&
  {
    return gpu_work_overlay_;
  }

  [[nodiscard]] auto TopInset() const noexcept -> float
  {
    return strip_visible_ ? strip_height_ : 0.0F;
  }

private:
  auto DrawStrip(IAsyncEngine& engine,
    observer_ptr<const vortex::Renderer> renderer,
    imgui::consoleui::ConsoleUiState& console_ui,
    const imgui::consoleui::LineRunner& run) -> void;

  imgui::DropdownMenuState view_mode_menu_;
  imgui::DropdownMenuState show_menu_;
  imgui::DropdownMenuState stats_menu_;
  imgui::StatsOverlay stats_overlay_;
  GpuWorkOverlay gpu_work_overlay_;
  float strip_height_ { 0.0F };
  bool strip_visible_ { false };
};

} // namespace oxygen::host
