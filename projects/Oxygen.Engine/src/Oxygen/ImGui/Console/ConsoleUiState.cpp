//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/ImGui/Console/ConsoleUiState.h>

namespace oxygen::imgui::consoleui {

auto to_string(const LogSeverity severity) -> const char*
{
  switch (severity) {
    // clang-format off
  case LogSeverity::kInfo: return "info";
  case LogSeverity::kSuccess: return "success";
  case LogSeverity::kWarning: return "warning";
  case LogSeverity::kError: return "error";
    // clang-format on
  }
  return "unknown";
}

auto ConsoleUiState::ToggleConsole() noexcept -> void
{
  SetConsoleVisible(!console_visible_);
}

auto ConsoleUiState::TogglePalette() noexcept -> void
{
  SetPaletteVisible(!palette_visible_);
}

auto ConsoleUiState::SetConsoleVisible(const bool visible) noexcept -> void
{
  console_visible_ = visible;
  if (visible) {
    request_console_focus_ = true;
  }
}

auto ConsoleUiState::SetPaletteVisible(const bool visible) noexcept -> void
{
  palette_visible_ = visible;
  if (visible) {
    request_palette_focus_ = true;
  }
}

auto ConsoleUiState::RequestConsoleFocus() noexcept -> void
{
  request_console_focus_ = true;
}

auto ConsoleUiState::RequestPaletteFocus() noexcept -> void
{
  request_palette_focus_ = true;
}

auto ConsoleUiState::IsConsoleVisible() const noexcept -> bool
{
  return console_visible_;
}

auto ConsoleUiState::IsPaletteVisible() const noexcept -> bool
{
  return palette_visible_;
}

auto ConsoleUiState::ConsumeConsoleFocusRequest() noexcept -> bool
{
  const bool requested = request_console_focus_;
  request_console_focus_ = false;
  return requested;
}

auto ConsoleUiState::ConsumePaletteFocusRequest() noexcept -> bool
{
  const bool requested = request_palette_focus_;
  request_palette_focus_ = false;
  return requested;
}

auto ConsoleUiState::SetCompletionPrefix(std::string prefix) -> void
{
  completion_prefix_ = std::move(prefix);
}

auto ConsoleUiState::CompletionPrefix() const -> const std::string&
{
  return completion_prefix_;
}

auto ConsoleUiState::ClearCompletion() -> void { completion_prefix_.clear(); }

auto ConsoleUiState::ResetHistoryNavigation() -> void
{
  history_cursor_ = kNoHistoryCursor;
  history_restore_line_.clear();
}

auto ConsoleUiState::HistoryCursor() const noexcept -> int
{
  return history_cursor_;
}

auto ConsoleUiState::SetHistoryCursor(const int cursor) noexcept -> void
{
  history_cursor_ = cursor;
}

auto ConsoleUiState::HistoryRestoreLine() const -> const std::string&
{
  return history_restore_line_;
}

auto ConsoleUiState::SetHistoryRestoreLine(std::string line) -> void
{
  history_restore_line_ = std::move(line);
}

auto ConsoleUiState::ConsoleInput() const -> const std::string&
{
  return console_input_;
}

auto ConsoleUiState::SetConsoleInput(std::string text) -> void
{
  console_input_ = std::move(text);
}

auto ConsoleUiState::PaletteQuery() const -> const std::string&
{
  return palette_query_;
}

auto ConsoleUiState::SetPaletteQuery(std::string query) -> void
{
  palette_query_ = std::move(query);
}

auto ConsoleUiState::PaletteCursor() const noexcept -> int
{
  return palette_cursor_;
}

auto ConsoleUiState::SetPaletteCursor(const int cursor) noexcept -> void
{
  palette_cursor_ = cursor;
}

auto ConsoleUiState::ConsoleWindowPlacement() const
  -> const std::optional<WindowPlacement>&
{
  return console_window_;
}

auto ConsoleUiState::SetConsoleWindowPlacement(
  const WindowPlacement placement) noexcept -> void
{
  console_window_ = placement;
}

auto ConsoleUiState::PaletteWindowPlacement() const
  -> const std::optional<WindowPlacement>&
{
  return palette_window_;
}

auto ConsoleUiState::SetPaletteWindowPlacement(
  const WindowPlacement placement) noexcept -> void
{
  palette_window_ = placement;
}

auto ConsoleUiState::IsAutoScrollEnabled() const noexcept -> bool
{
  return auto_scroll_;
}

auto ConsoleUiState::SetAutoScrollEnabled(const bool enabled) noexcept -> void
{
  auto_scroll_ = enabled;
}

auto ConsoleUiState::IsSeverityEnabled(
  const LogSeverity severity) const noexcept -> bool
{
  return severity_enabled_.at(SeverityIndex(severity));
}

auto ConsoleUiState::SetSeverityEnabled(
  const LogSeverity severity, const bool enabled) noexcept -> void
{
  severity_enabled_.at(SeverityIndex(severity)) = enabled;
}

auto ConsoleUiState::AppendLogEntry(
  std::string command, const oxygen::console::ExecutionResult& result) -> void
{
  if (log_entries_.size() >= kMaxLogEntries) {
    log_entries_.erase(log_entries_.begin());
  }
  log_entries_.push_back(ConsoleLogEntry {
    .sequence = next_sequence_++,
    .command = std::move(command),
    .result = result,
    .severity = SeverityFromResult(result),
  });
}

auto ConsoleUiState::LogEntries() const -> const std::vector<ConsoleLogEntry>&
{
  return log_entries_;
}

auto ConsoleUiState::ClearLogEntries() -> void { log_entries_.clear(); }

auto ConsoleUiState::SeverityFromResult(
  const oxygen::console::ExecutionResult& result) noexcept -> LogSeverity
{
  using oxygen::console::ExecutionStatus;
  switch (result.status) {
  case ExecutionStatus::kOk:
    return result.error.empty() ? LogSeverity::kSuccess : LogSeverity::kWarning;
  case ExecutionStatus::kDenied:
    return LogSeverity::kWarning;
  case ExecutionStatus::kNotFound:
  case ExecutionStatus::kInvalidArguments:
  case ExecutionStatus::kError:
    return LogSeverity::kError;
  }
  return LogSeverity::kInfo;
}

auto ConsoleUiState::SeverityIndex(const LogSeverity severity) noexcept
  -> size_t
{
  switch (severity) {
  case LogSeverity::kInfo:
    return 0;
  case LogSeverity::kSuccess:
    return 1;
  case LogSeverity::kWarning:
    return 2;
  case LogSeverity::kError:
    return 3;
  }
  return 0;
}

} // namespace oxygen::imgui::consoleui
