//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <imgui.h>

#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/ImGui/Console/ConsoleHost.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>

namespace oxygen::imgui::consoleui {

namespace {

  using commands::CommandArgs;
  using commands::CommandContext;
  using commands::CommandDefinition;
  using commands::CommandParam;
  using commands::CommandResult;
  using commands::ParamType;
  using commands::ParamValue;

  //! The hotkeys decide their action when pressed, from the state the user
  //! saw, and post it.
  auto HandleHotkeys(const ConsoleUiState& state, const LineRunner& run) -> void
  {
    // Processed regardless of ImGui capture flags so the console can always
    // be reached.
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
      if (!state.IsConsoleVisible()) {
        run("ui.console open", commands::CommandSource::kKey);
      } else if (state.ConsoleInput().empty()) {
        run("ui.console close", commands::CommandSource::kKey);
      } else {
        run("ui.console focus", commands::CommandSource::kKey);
      }
    }
    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
      run("ui.palette", commands::CommandSource::kKey);
    }
  }

  auto ActionParam(std::vector<std::string> actions) -> CommandParam
  {
    return CommandParam {
      .name = "action",
      .type = ParamType::kEnum,
      .default_value = ParamValue { std::string { "toggle" } },
      .allowed_values = std::move(actions),
    };
  }

} // namespace

ConsoleHost::~ConsoleHost() { UnregisterCommands(); }

auto ConsoleHost::Draw(console::Console& console, ImGuiContext* imgui_context,
  const LineRunner& run) -> void
{
  if (imgui_context == nullptr) {
    return;
  }
  ImGui::SetCurrentContext(imgui_context);
  HandleHotkeys(state_, run);
  panel_.Draw(console, state_, imgui_context, run);
  palette_.Draw(console, state_, imgui_context, run);
}

auto ConsoleHost::RegisterCommands(commands::CommandRegistry& registry) -> void
{
  UnregisterCommands();
  registry_ = observer_ptr { &registry };
  handles_.push_back(registry.Register(CommandDefinition {
    .name = "ui.console",
    .label = "Console",
    .help = "Show, hide or focus the console",
    .params = { ActionParam({ "toggle", "open", "close", "focus" }) },
    .handler
    = [this](const CommandArgs& args, const CommandContext&) -> CommandResult {
      const auto& action = args.GetString("action");
      if (action == "toggle") {
        state_.ToggleConsole();
      } else if (action == "close") {
        state_.SetConsoleVisible(false);
      } else {
        state_.SetConsoleVisible(true);
        state_.RequestConsoleFocus();
      }
      return CommandResult::Success();
    },
  }));
  handles_.push_back(registry.Register(CommandDefinition {
    .name = "ui.palette",
    .label = "Command Palette",
    .help = "Show or hide the command palette",
    .params = { ActionParam({ "toggle", "open", "close" }) },
    .handler
    = [this](const CommandArgs& args, const CommandContext&) -> CommandResult {
      const auto& action = args.GetString("action");
      if (action == "toggle") {
        state_.TogglePalette();
      } else {
        state_.SetPaletteVisible(action == "open");
      }
      return CommandResult::Success();
    },
  }));
}

auto ConsoleHost::UnregisterCommands() noexcept -> void
{
  if (registry_ != nullptr) {
    for (const auto handle : handles_) {
      static_cast<void>(registry_->Unregister(handle));
    }
  }
  handles_.clear();
  registry_ = nullptr;
}

} // namespace oxygen::imgui::consoleui
