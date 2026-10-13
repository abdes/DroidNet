//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/ImGui/Console/CommandPalette.h>
#include <Oxygen/ImGui/Console/ConsolePanel.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>
#include <Oxygen/ImGui/api_export.h>

struct ImGuiContext;

namespace oxygen::console {
class Console;
} // namespace oxygen::console

namespace oxygen::imgui::consoleui {

//! The console window and the command palette with their hotkeys.
/*!
 Owns the console and palette UI state and the commands that change it:
 `ui.console [toggle|open|close|focus]` and `ui.palette [toggle|open|close]`.
 Backtick posts `ui.console` (opens the console, closes it when its input
 line is empty, otherwise focuses it) and Ctrl+Shift+P posts `ui.palette`;
 the hotkeys work whatever ImGui is capturing. Lines typed in the console or
 picked in the palette go to the `LineRunner`. Window placement is held in
 `State()`; an application that persists it reads and restores it there.
*/
class ConsoleHost final {
public:
  OXGN_IMGUI_API ConsoleHost() = default;
  OXGN_IMGUI_API ~ConsoleHost();

  OXYGEN_MAKE_NON_COPYABLE(ConsoleHost)
  OXYGEN_MAKE_NON_MOVABLE(ConsoleHost)

  //! Handles the hotkeys and draws both windows. Call once per ImGui frame.
  OXGN_IMGUI_API auto Draw(console::Console& console,
    ImGuiContext* imgui_context, const LineRunner& run) -> void;

  //! Registers `ui.console` and `ui.palette`; they stay until
  //! `UnregisterCommands` or destruction, so the registry must outlive this.
  OXGN_IMGUI_API auto RegisterCommands(commands::CommandRegistry& registry)
    -> void;
  OXGN_IMGUI_API auto UnregisterCommands() noexcept -> void;

  [[nodiscard]] auto State() noexcept -> ConsoleUiState& { return state_; }
  [[nodiscard]] auto State() const noexcept -> const ConsoleUiState&
  {
    return state_;
  }

private:
  ConsoleUiState state_;
  ConsolePanel panel_;
  CommandPalette palette_;
  observer_ptr<commands::CommandRegistry> registry_;
  std::vector<commands::CommandHandle> handles_;
};

} // namespace oxygen::imgui::consoleui
