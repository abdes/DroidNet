//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/ImGui/api_export.h>

namespace oxygen::imgui::consoleui {

enum class LogSeverity : uint8_t {
  kInfo,
  kSuccess,
  kWarning,
  kError,
};

struct ConsoleLogEntry final {
  uint64_t sequence { 0 };
  std::string command;
  oxygen::console::ExecutionResult result;
  LogSeverity severity { LogSeverity::kInfo };
};

OXGN_IMGUI_NDAPI auto to_string(LogSeverity severity) -> const char*;

struct WindowPlacement final {
  float x { 0.0F };
  float y { 0.0F };
  float width { 0.0F };
  float height { 0.0F };
};

class ConsoleUiState final {
public:
  OXGN_IMGUI_API ConsoleUiState() = default;
  ~ConsoleUiState() = default;

  OXYGEN_DEFAULT_COPYABLE(ConsoleUiState)
  OXYGEN_DEFAULT_MOVABLE(ConsoleUiState)

  OXGN_IMGUI_API auto ToggleConsole() noexcept -> void;
  OXGN_IMGUI_API auto TogglePalette() noexcept -> void;
  OXGN_IMGUI_API auto SetConsoleVisible(bool visible) noexcept -> void;
  OXGN_IMGUI_API auto SetPaletteVisible(bool visible) noexcept -> void;
  OXGN_IMGUI_API auto RequestConsoleFocus() noexcept -> void;
  OXGN_IMGUI_API auto RequestPaletteFocus() noexcept -> void;
  OXGN_IMGUI_NDAPI auto IsConsoleVisible() const noexcept -> bool;
  OXGN_IMGUI_NDAPI auto IsPaletteVisible() const noexcept -> bool;
  OXGN_IMGUI_NDAPI auto ConsumeConsoleFocusRequest() noexcept -> bool;
  OXGN_IMGUI_NDAPI auto ConsumePaletteFocusRequest() noexcept -> bool;

  OXGN_IMGUI_API auto SetCompletionPrefix(std::string prefix) -> void;
  OXGN_IMGUI_NDAPI auto CompletionPrefix() const -> const std::string&;
  OXGN_IMGUI_API auto ClearCompletion() -> void;

  OXGN_IMGUI_API auto ResetHistoryNavigation() -> void;
  OXGN_IMGUI_NDAPI auto HistoryCursor() const noexcept -> int;
  OXGN_IMGUI_API auto SetHistoryCursor(int cursor) noexcept -> void;
  OXGN_IMGUI_NDAPI auto HistoryRestoreLine() const -> const std::string&;
  OXGN_IMGUI_API auto SetHistoryRestoreLine(std::string line) -> void;

  OXGN_IMGUI_NDAPI auto ConsoleInput() const -> const std::string&;
  OXGN_IMGUI_API auto SetConsoleInput(std::string text) -> void;
  OXGN_IMGUI_NDAPI auto PaletteQuery() const -> const std::string&;
  OXGN_IMGUI_API auto SetPaletteQuery(std::string query) -> void;
  OXGN_IMGUI_NDAPI auto PaletteCursor() const noexcept -> int;
  OXGN_IMGUI_API auto SetPaletteCursor(int cursor) noexcept -> void;
  OXGN_IMGUI_NDAPI auto ConsoleWindowPlacement() const
    -> const std::optional<WindowPlacement>&;
  OXGN_IMGUI_API auto SetConsoleWindowPlacement(
    WindowPlacement placement) noexcept -> void;
  OXGN_IMGUI_NDAPI auto PaletteWindowPlacement() const
    -> const std::optional<WindowPlacement>&;
  OXGN_IMGUI_API auto SetPaletteWindowPlacement(
    WindowPlacement placement) noexcept -> void;

  OXGN_IMGUI_NDAPI auto IsAutoScrollEnabled() const noexcept -> bool;
  OXGN_IMGUI_API auto SetAutoScrollEnabled(bool enabled) noexcept -> void;
  OXGN_IMGUI_NDAPI auto IsSeverityEnabled(LogSeverity severity) const noexcept
    -> bool;
  OXGN_IMGUI_API auto SetSeverityEnabled(
    LogSeverity severity, bool enabled) noexcept -> void;

  OXGN_IMGUI_API auto AppendLogEntry(std::string command,
    const oxygen::console::ExecutionResult& result) -> void;
  OXGN_IMGUI_NDAPI auto LogEntries() const
    -> const std::vector<ConsoleLogEntry>&;
  OXGN_IMGUI_API auto ClearLogEntries() -> void;

private:
  OXGN_IMGUI_NDAPI static auto SeverityFromResult(
    const oxygen::console::ExecutionResult& result) noexcept -> LogSeverity;
  OXGN_IMGUI_NDAPI static auto SeverityIndex(LogSeverity severity) noexcept
    -> size_t;

  static constexpr size_t kSeverityCount = 4;
  static constexpr size_t kMaxLogEntries = 2048;
  static constexpr int kNoHistoryCursor = -1;

  bool console_visible_ { false };
  bool palette_visible_ { false };
  bool request_console_focus_ { false };
  bool request_palette_focus_ { false };
  bool auto_scroll_ { true };
  std::array<bool, kSeverityCount> severity_enabled_ {
    true,
    true,
    true,
    true,
  };
  uint64_t next_sequence_ { 1 };
  std::vector<ConsoleLogEntry> log_entries_;

  std::string completion_prefix_;
  std::string history_restore_line_;
  int history_cursor_ { kNoHistoryCursor };
  std::string console_input_;
  std::string palette_query_;
  int palette_cursor_ { 0 };
  std::optional<WindowPlacement> console_window_;
  std::optional<WindowPlacement> palette_window_;
};

} // namespace oxygen::imgui::consoleui
