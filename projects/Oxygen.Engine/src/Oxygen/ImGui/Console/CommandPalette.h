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

namespace oxygen::imgui::consoleui {

class CommandPalette final {
public:
  OXGN_IMGUI_API CommandPalette() = default;
  ~CommandPalette() = default;

  OXYGEN_DEFAULT_COPYABLE(CommandPalette)
  OXYGEN_DEFAULT_MOVABLE(CommandPalette)

  OXGN_IMGUI_API auto Draw(
    oxygen::console::Console& console, ConsoleUiState& state) -> void;
};

} // namespace oxygen::imgui::consoleui
