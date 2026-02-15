//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

#include <imgui.h>

#include <Oxygen/ImGui/Console/CommandPalette.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg) - ImGui

namespace oxygen::imgui::consoleui {

namespace {

  constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoCollapse
    | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar
    | ImGuiWindowFlags_NoTitleBar;
  constexpr float kPaletteWidth = 720.0F;
  constexpr float kPaletteHeight = 460.0F;
  constexpr ImVec2 kWindowPadding = ImVec2(8.0F, 8.0F);
  constexpr float kResultsTopSpacing = 4.0F;
  constexpr float kResultsBottomReserve = 4.0F;
  constexpr int kNoMatchRank = 1000;
  constexpr int kPrefixRank = 0;
  constexpr int kFuzzyRank = 1;
  constexpr int kNoSelection = -1;
  constexpr size_t kQueryBufferSize = 256;
  constexpr ImVec2 kCenterPivot = ImVec2(0.5F, 0.5F);

  struct ScoredSymbol final {
    oxygen::console::ConsoleSymbol symbol;
    int match_rank { kNoMatchRank };
  };

  auto ToLower(std::string value) -> std::string
  {
    std::ranges::transform(value, value.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
  }

  struct MatchQuery {
    std::string_view query;
    std::string_view target;
  };

  auto IsSubsequenceMatch(const MatchQuery& match) -> bool
  {
    if (match.query.empty()) {
      return true;
    }
    size_t query_index = 0;
    for (const char c : match.target) {
      if (query_index >= match.query.size()) {
        break;
      }
      if (match.query[query_index] == c) {
        ++query_index;
      }
    }
    return query_index == match.query.size();
  }

  auto KindLabel(const oxygen::console::CompletionKind kind) -> const char*
  {
    switch (kind) {
    case oxygen::console::CompletionKind::kCommand:
      return "cmd";
    case oxygen::console::CompletionKind::kCVar:
      return "cvar";
    }
    return "unknown";
  }

} // namespace

auto CommandPalette::Draw(oxygen::console::Console& console,
  ConsoleUiState& state, ImGuiContext* imgui_context) -> void
{
  if (imgui_context == nullptr) {
    return;
  }
  ImGui::SetCurrentContext(imgui_context);

  if (!state.IsPaletteVisible()) {
    return;
  }

  if (const auto& placement = state.PaletteWindowPlacement();
    placement.has_value()) {
    ImGui::SetNextWindowPos(
      ImVec2(placement->x, placement->y), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(
      ImVec2(placement->width, placement->height), ImGuiCond_Appearing);
  } else {
    ImGui::SetNextWindowSize(
      ImVec2(kPaletteWidth, kPaletteHeight), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(
      ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, kCenterPivot);
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, kWindowPadding);
  if (!ImGui::Begin("Command Palette", nullptr, kWindowFlags)) {
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }
  const auto window_pos = ImGui::GetWindowPos();
  const auto window_size = ImGui::GetWindowSize();
  state.SetPaletteWindowPlacement(WindowPlacement {
    .x = window_pos.x,
    .y = window_pos.y,
    .width = window_size.x,
    .height = window_size.y,
  });

  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    state.SetPaletteVisible(false);
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }

  std::array<char, kQueryBufferSize> query_buffer {};
  const auto& query_text = state.PaletteQuery();
  const auto query_length = std::min(query_text.size(), kQueryBufferSize - 1);
  std::copy_n(query_text.data(), query_length, query_buffer.data());
  query_buffer.at(query_length) = '\0';

  std::string query(query_buffer.data());
  if (state.ConsumePaletteFocusRequest()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::InputTextWithHint("##PaletteQuery", "Type a command or cvar...",
        query_buffer.data(), query_buffer.size())) {
    query = query_buffer.data();
    state.SetPaletteQuery(query);
    state.SetPaletteCursor(0);
  }

  const auto lowered_query = ToLower(query);
  auto symbols = console.ListSymbols(false);
  std::vector<ScoredSymbol> results;
  results.reserve(symbols.size());
  for (auto& symbol : symbols) {
    const auto lowered_token = ToLower(symbol.token);
    if (lowered_query.empty()) {
      results.push_back(ScoredSymbol {
        .symbol = std::move(symbol), .match_rank = kPrefixRank });
      continue;
    }
    if (lowered_token.starts_with(lowered_query)) {
      results.push_back(ScoredSymbol {
        .symbol = std::move(symbol), .match_rank = kPrefixRank });
      continue;
    }
    if (IsSubsequenceMatch(
          { .query = lowered_query, .target = lowered_token })) {
      results.push_back(
        ScoredSymbol { .symbol = std::move(symbol), .match_rank = kFuzzyRank });
    }
  }

  std::sort(results.begin(), results.end(),
    [](const ScoredSymbol& lhs, const ScoredSymbol& rhs) {
      if (lhs.match_rank != rhs.match_rank) {
        return lhs.match_rank < rhs.match_rank;
      }
      if (lhs.symbol.usage_frequency != rhs.symbol.usage_frequency) {
        return lhs.symbol.usage_frequency > rhs.symbol.usage_frequency;
      }
      if (lhs.symbol.usage_last_tick != rhs.symbol.usage_last_tick) {
        return lhs.symbol.usage_last_tick > rhs.symbol.usage_last_tick;
      }
      return lhs.symbol.token < rhs.symbol.token;
    });

  if (results.empty()) {
    state.SetPaletteCursor(kNoSelection);
    ImGui::Spacing();
    ImGui::TextDisabled("No matches");
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }

  int cursor = state.PaletteCursor();
  if (cursor < 0 || cursor >= static_cast<int>(results.size())) {
    cursor = 0;
  }
  bool cursor_moved_by_keyboard = false;
  bool moved_up = false;
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
    cursor = std::max(0, cursor - 1);
    cursor_moved_by_keyboard = true;
    moved_up = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
    cursor = std::min(static_cast<int>(results.size()) - 1, cursor + 1);
    cursor_moved_by_keyboard = true;
  }
  state.SetPaletteCursor(cursor);

  bool execute_selected = false;
  if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    execute_selected = true;
  }

  ImGui::Dummy(ImVec2(0.0F, kResultsTopSpacing));
  const auto results_height
    = std::max(0.0F, ImGui::GetContentRegionAvail().y - kResultsBottomReserve);
  if (ImGui::BeginChild("PaletteResults", ImVec2(0.0F, results_height),
        ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
    for (size_t i = 0; i < results.size(); ++i) {
      const bool selected = static_cast<int>(i) == cursor;
      const auto& symbol = results[i].symbol;
      std::string line
        = std::string(KindLabel(symbol.kind)) + "  " + symbol.token;
      if (ImGui::Selectable(line.c_str(), selected)) {
        cursor = static_cast<int>(i);
        state.SetPaletteCursor(cursor);
        execute_selected = true;
      }
      if (selected && cursor_moved_by_keyboard && !ImGui::IsItemVisible()) {
        ImGui::SetScrollHereY(moved_up ? 0.0F : 1.0F);
      }
      if (!symbol.help.empty()) {
        ImGui::TextDisabled("    %s", symbol.help.c_str());
      }
    }
  }
  ImGui::EndChild();

  if (execute_selected) {
    const auto& current
      = results[static_cast<size_t>(state.PaletteCursor())].symbol;
    const auto result = console.Execute(current.token);
    state.AppendLogEntry(current.token, result);
    state.SetConsoleVisible(true);
    state.SetPaletteVisible(false);
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace oxygen::imgui::consoleui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
