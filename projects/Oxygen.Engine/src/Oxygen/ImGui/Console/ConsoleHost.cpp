//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Console/Console.h>
#include <Oxygen/ImGui/Console/ConsoleHost.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>

namespace oxygen::imgui::consoleui {

namespace {

  auto HandleHotkeys(ConsoleUiState& state) -> void
  {
    // Processed regardless of ImGui capture flags so the console can always
    // be reached.
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
      if (!state.IsConsoleVisible()) {
        state.SetConsoleVisible(true);
      } else if (state.ConsoleInput().empty()) {
        state.SetConsoleVisible(false);
      } else {
        state.RequestConsoleFocus();
      }
    }
    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
      state.TogglePalette();
    }
  }

} // namespace

auto ConsoleHost::Draw(console::Console& console, ImGuiContext* imgui_context)
  -> void
{
  if (imgui_context == nullptr) {
    return;
  }
  ImGui::SetCurrentContext(imgui_context);
  HandleHotkeys(state_);
  panel_.Draw(console, state_, imgui_context);
  palette_.Draw(console, state_, imgui_context);
}

} // namespace oxygen::imgui::consoleui
