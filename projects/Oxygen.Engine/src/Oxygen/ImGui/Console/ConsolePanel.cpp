//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <string_view>

#include <imgui.h>

#include <Oxygen/ImGui/Console/ConsolePanel.h>

namespace oxygen::imgui::consoleui {

namespace {

  constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoCollapse;
  constexpr ImGuiInputTextFlags kInputFlags
    = ImGuiInputTextFlags_EnterReturnsTrue
    | ImGuiInputTextFlags_CallbackCompletion
    | ImGuiInputTextFlags_CallbackHistory;
  constexpr float kPanelDefaultWidth = 900.0F;
  constexpr float kPanelDefaultHeight = 420.0F;
  constexpr float kInputWidth = -1.0F;
  constexpr float kLogHeightReserve = 90.0F;
  constexpr size_t kInputBufferSize = 1024;
  constexpr int kNoHistoryCursor = -1;

  struct ConsoleInputCallbackContext final {
    oxygen::console::Console* console { nullptr };
    ConsoleUiState* state { nullptr };
  };

  auto SeverityColor(const LogSeverity severity) -> ImVec4
  {
    switch (severity) {
    case LogSeverity::kInfo:
      return { 0.85F, 0.85F, 0.90F, 1.00F };
    case LogSeverity::kSuccess:
      return { 0.40F, 0.85F, 0.45F, 1.00F };
    case LogSeverity::kWarning:
      return { 0.95F, 0.80F, 0.30F, 1.00F };
    case LogSeverity::kError:
      return { 0.95F, 0.38F, 0.38F, 1.00F };
    }
    return { 1.0F, 1.0F, 1.0F, 1.0F };
  }

  auto SeverityLabel(const LogSeverity severity) -> const char*
  {
    switch (severity) {
    case LogSeverity::kInfo:
      return "Info";
    case LogSeverity::kSuccess:
      return "Ok";
    case LogSeverity::kWarning:
      return "Warning";
    case LogSeverity::kError:
      return "Error";
    }
    return "Unknown";
  }

} // namespace

auto ConsolePanel::Draw(oxygen::console::Console& console,
  ConsoleUiState& state, ImGuiContext* imgui_context) -> void
{
  if (imgui_context == nullptr) {
    return;
  }
  ImGui::SetCurrentContext(imgui_context);

  if (!state.IsConsoleVisible()) {
    return;
  }

  if (const auto& placement = state.ConsoleWindowPlacement();
    placement.has_value()) {
    ImGui::SetNextWindowPos(
      ImVec2(placement->x, placement->y), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(
      ImVec2(placement->width, placement->height), ImGuiCond_Appearing);
  } else {
    ImGui::SetNextWindowSize(
      ImVec2(kPanelDefaultWidth, kPanelDefaultHeight), ImGuiCond_FirstUseEver);
  }
  if (!ImGui::Begin("Console", nullptr, kPanelFlags)) {
    ImGui::End();
    return;
  }
  const auto window_pos = ImGui::GetWindowPos();
  const auto window_size = ImGui::GetWindowSize();
  state.SetConsoleWindowPlacement(WindowPlacement {
    .x = window_pos.x,
    .y = window_pos.y,
    .width = window_size.x,
    .height = window_size.y,
  });

  bool auto_scroll = state.IsAutoScrollEnabled();
  if (ImGui::Checkbox("Auto Scroll", &auto_scroll)) {
    state.SetAutoScrollEnabled(auto_scroll);
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    state.ClearLogEntries();
    console.ClearExecutionRecords();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("Filters:");

  std::array<LogSeverity, 3> severities {
    LogSeverity::kSuccess,
    LogSeverity::kWarning,
    LogSeverity::kError,
  };
  for (const auto severity : severities) {
    ImGui::SameLine();
    bool enabled = state.IsSeverityEnabled(severity);
    if (ImGui::Checkbox(SeverityLabel(severity), &enabled)) {
      state.SetSeverityEnabled(severity, enabled);
    }
  }

  if (ImGui::BeginChild("ConsoleLog", ImVec2(0.0F, -kLogHeightReserve), true)) {
    for (const auto& entry : state.LogEntries()) {
      if (!state.IsSeverityEnabled(entry.severity)) {
        continue;
      }

      const auto command_label = "> " + entry.command;
      const auto command_id
        = command_label + "##cmd" + std::to_string(entry.sequence);
      const auto popup_id = "cmd_ctx##" + std::to_string(entry.sequence);
      const bool selected = ImGui::Selectable(command_id.c_str(), false);
      if (selected && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        RunCommand(console, state, entry.command);
      }
      if (ImGui::BeginPopupContextItem(popup_id.c_str())) {
        if (ImGui::MenuItem("Run")) {
          RunCommand(console, state, entry.command);
        }
        if (ImGui::MenuItem("Copy Command")) {
          ImGui::SetClipboardText(entry.command.c_str());
        }
        ImGui::EndPopup();
      }

      const auto color = SeverityColor(entry.severity);
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted(SeverityLabel(entry.severity));
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (!entry.result.error.empty()) {
        ImGui::TextUnformatted(entry.result.error.c_str());
      } else if (!entry.result.output.empty()) {
        ImGui::TextUnformatted(entry.result.output.c_str());
      } else {
        ImGui::TextUnformatted("");
      }
      ImGui::Spacing();
    }

    if (state.IsAutoScrollEnabled()
      && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0F);
    }
  }
  ImGui::EndChild();

  if (state.ConsumeConsoleFocusRequest()) {
    ImGui::SetKeyboardFocusHere();
  }
  std::array<char, kInputBufferSize> input_buffer {};
  const auto& input = state.ConsoleInput();
  const auto copy_length = std::min(input.size(), kInputBufferSize - 1);
  std::copy_n(input.data(), copy_length, input_buffer.data());
  input_buffer[copy_length] = '\0';

  ImGui::SetNextItemWidth(kInputWidth);
  ConsoleInputCallbackContext callback_context {
    .console = &console,
    .state = &state,
  };
  const bool execute = ImGui::InputTextWithHint("##ConsoleInput",
    "type command and press Enter", input_buffer.data(), input_buffer.size(),
    kInputFlags, &ConsolePanel::ConsoleInputCallback, &callback_context);
  const auto input_line = std::string(input_buffer.data());
  if (ImGui::IsItemActivated()) {
    state.ResetHistoryNavigation();
  }
  if (ImGui::IsItemFocused() && input_line.empty()
    && ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
    state.SetConsoleVisible(false);
    ImGui::End();
    return;
  }
  if (ImGui::IsItemEdited()) {
    state.SetConsoleInput(input_line);
  }
  if (execute) {
    RunCommand(console, state, input_line);
  } else {
    state.SetConsoleInput(input_line);
  }

  ImGui::End();
}

auto ConsolePanel::ConsoleInputCallback(ImGuiInputTextCallbackData* data) -> int
{
  auto* context = static_cast<ConsoleInputCallbackContext*>(data->UserData);
  if (context == nullptr || context->console == nullptr
    || context->state == nullptr) {
    return 0;
  }

  auto& console = *context->console;
  auto& state = *context->state;

  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
    const auto input
      = std::string_view(data->Buf, static_cast<size_t>(data->BufTextLen));
    observer_ptr<const oxygen::console::CompletionCandidate> candidate {};
    bool should_cycle = false;
    if (!state.CompletionPrefix().empty()) {
      if (state.CompletionPrefix() == input) {
        should_cycle = true;
      } else if (const auto current = console.CurrentCompletion();
        current != nullptr) {
        auto expanded = current->token;
        expanded.push_back(' ');
        should_cycle = expanded == input;
      }
    }

    if (should_cycle) {
      candidate = console.NextCompletion();
    } else {
      state.SetCompletionPrefix(std::string(input));
      candidate = console.BeginCompletionCycle(input);
    }
    if (candidate != nullptr) {
      const auto replacement = candidate->token + " ";
      data->DeleteChars(0, data->BufTextLen);
      data->InsertChars(0, replacement.c_str());
      state.SetConsoleInput(replacement);
    }
    return 0;
  }

  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    const auto& entries = console.GetHistory().Entries();
    if (entries.empty()) {
      return 0;
    }

    const int history_size = static_cast<int>(entries.size());
    int cursor = state.HistoryCursor();
    if (data->EventKey == ImGuiKey_UpArrow) {
      if (cursor == kNoHistoryCursor) {
        state.SetHistoryRestoreLine(
          std::string(data->Buf, static_cast<size_t>(data->BufTextLen)));
        cursor = history_size - 1;
      } else {
        cursor = std::max(0, cursor - 1);
      }
      state.SetHistoryCursor(cursor);
    } else if (data->EventKey == ImGuiKey_DownArrow) {
      if (cursor == kNoHistoryCursor) {
        return 0;
      }
      ++cursor;
      if (cursor >= history_size) {
        state.SetHistoryCursor(kNoHistoryCursor);
        const auto& restore = state.HistoryRestoreLine();
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, restore.c_str());
        state.SetConsoleInput(restore);
        state.SetHistoryRestoreLine({});
        return 0;
      }
      state.SetHistoryCursor(cursor);
    }

    if (const int resolved_cursor = state.HistoryCursor();
      resolved_cursor >= 0 && resolved_cursor < history_size) {
      const auto& replacement = entries[static_cast<size_t>(resolved_cursor)];
      data->DeleteChars(0, data->BufTextLen);
      data->InsertChars(0, replacement.c_str());
      state.SetConsoleInput(replacement);
    }
  }

  return 0;
}

auto ConsolePanel::RunCommand(oxygen::console::Console& console,
  ConsoleUiState& state, const std::string& line) -> void
{
  if (line.empty()) {
    return;
  }

  const auto result = console.Execute(line);
  state.AppendLogEntry(line, result);
  state.SetConsoleInput({});
  state.ClearCompletion();
  state.ResetHistoryNavigation();
  state.RequestConsoleFocus();
}

} // namespace oxygen::imgui::consoleui
