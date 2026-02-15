//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>
#include <Oxygen/ImGui/api_export.h>

struct ImGuiInputTextCallbackData;
struct ImGuiContext;

namespace oxygen::imgui::consoleui {

class ConsolePanel final {
public:
  OXGN_IMGUI_API ConsolePanel() = default;
  ~ConsolePanel() = default;

  OXYGEN_DEFAULT_COPYABLE(ConsolePanel)
  OXYGEN_DEFAULT_MOVABLE(ConsolePanel)

  OXGN_IMGUI_API auto Draw(oxygen::console::Console& console,
    ConsoleUiState& state, ImGuiContext* imgui_context) -> void;

private:
  OXGN_IMGUI_API static auto ConsoleInputCallback(
    ImGuiInputTextCallbackData* data) -> int;
  OXGN_IMGUI_API static auto RunCommand(oxygen::console::Console& console,
    ConsoleUiState& state, const std::string& line) -> void;
};

} // namespace oxygen::imgui::consoleui
