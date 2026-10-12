//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
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
 Backtick opens the console, closes it when its input line is empty, and
 otherwise focuses it; Ctrl+Shift+P toggles the palette. The hotkeys work
 whatever ImGui is capturing. Window placement is held in `State()`; an
 application that persists it reads and restores it there.
*/
class ConsoleHost final {
public:
  OXGN_IMGUI_API ConsoleHost() = default;
  ~ConsoleHost() = default;

  OXYGEN_DEFAULT_COPYABLE(ConsoleHost)
  OXYGEN_DEFAULT_MOVABLE(ConsoleHost)

  //! Handles the hotkeys and draws both windows. Call once per ImGui frame.
  OXGN_IMGUI_API auto Draw(
    console::Console& console, ImGuiContext* imgui_context) -> void;

  [[nodiscard]] auto State() noexcept -> ConsoleUiState& { return state_; }
  [[nodiscard]] auto State() const noexcept -> const ConsoleUiState&
  {
    return state_;
  }

private:
  ConsoleUiState state_;
  ConsolePanel panel_;
  CommandPalette palette_;
};

} // namespace oxygen::imgui::consoleui
