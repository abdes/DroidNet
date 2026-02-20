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

#include <fmt/format.h>
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

  auto ValueToString(const oxygen::console::CVarValue& value) -> std::string
  {
    return std::visit(
      [](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double>) {
          return fmt::format("{:.2f}", v);
        } else if constexpr (std::is_same_v<T, bool>) {
          return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return fmt::format("{}", v);
        } else {
          return std::string(v);
        }
      },
      value);
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

  if (state.ConsumePaletteFocusRequest()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::InputTextWithHint("##PaletteQuery", "Type a command or cvar...",
        query_buffer.data(), query_buffer.size())) {
    state.SetPaletteQuery(query_buffer.data());
    state.SetPaletteCursor(0);
  }

  // Logic: Split query into search_term and arguments
  const std::string full_query = query_buffer.data();
  std::string search_term = full_query;
  std::string arguments;
  if (const auto space_pos = full_query.find(' ');
    space_pos != std::string::npos) {
    search_term = full_query.substr(0, space_pos);
    arguments = full_query.substr(space_pos + 1);
  }

  const auto lowered_search = ToLower(search_term);
  auto symbols = console.ListSymbols(false);
  std::vector<ScoredSymbol> results;
  results.reserve(symbols.size());
  for (auto& symbol : symbols) {
    const auto lowered_token = ToLower(symbol.token);
    if (lowered_search.empty()) {
      results.push_back(ScoredSymbol {
        .symbol = std::move(symbol), .match_rank = kPrefixRank });
      continue;
    }
    if (lowered_token.starts_with(lowered_search)) {
      results.push_back(ScoredSymbol {
        .symbol = std::move(symbol), .match_rank = kPrefixRank });
      continue;
    }
    if (IsSubsequenceMatch(
          { .query = lowered_search, .target = lowered_token })) {
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

  const auto& current_symbol = results[static_cast<size_t>(cursor)].symbol;

  bool execute_selected = false;
  if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
    execute_selected = true;
  }

  // Tab completion: Fill query with selected token and stay open
  if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
    state.SetPaletteQuery(current_symbol.token + " ");
    state.RequestPaletteFocus();
  }

  ImGui::Dummy(ImVec2(0.0F, kResultsTopSpacing));
  const auto results_height
    = std::max(0.0F, ImGui::GetContentRegionAvail().y - kResultsBottomReserve);
  if (ImGui::BeginChild("PaletteResults", ImVec2(0.0F, results_height),
        ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
    for (size_t i = 0; i < results.size(); ++i) {
      const bool selected = static_cast<int>(i) == cursor;
      const auto& symbol = results[i].symbol;

      const auto kind_str = std::string(KindLabel(symbol.kind));
      ImVec4 kind_color = symbol.kind == oxygen::console::CompletionKind::kCVar
        ? ImVec4(0.4F, 0.7F, 1.0F, 1.0F) // Light Blue for CVars
        : ImVec4(0.4F, 1.0F, 0.4F, 1.0F); // Light Green for Commands

      ImGui::PushID(static_cast<int>(i));
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kind_color, "[%s]", kind_str.c_str());
      ImGui::SameLine();

      if (ImGui::Selectable(symbol.token.c_str(), selected)) {
        cursor = static_cast<int>(i);
        state.SetPaletteCursor(cursor);
        execute_selected = true;
      }

      if (symbol.kind == oxygen::console::CompletionKind::kCVar) {
        if (const auto snapshot = console.FindCVar(symbol.token)) {
          ImGui::SameLine();
          ImGui::TextDisabled(
            "= %s", ValueToString(snapshot->current_value).c_str());

          // State Badges
          std::string badges;
          if (HasFlag(snapshot->definition.flags,
                oxygen::console::CVarFlags::kArchive))
            badges += " [A]";
          if (HasFlag(snapshot->definition.flags,
                oxygen::console::CVarFlags::kReadOnly))
            badges += " [R]";
          if (HasFlag(snapshot->definition.flags,
                oxygen::console::CVarFlags::kRequiresRestart))
            badges += " [!]";

          if (!badges.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(
              ImVec4(1.0F, 0.8F, 0.2F, 1.0F), "%s", badges.c_str());
          }

          if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Default: %s",
              ValueToString(snapshot->definition.default_value).c_str());
            if (snapshot->definition.min_value)
              ImGui::Text("Min: %.2f", *snapshot->definition.min_value);
            if (snapshot->definition.max_value)
              ImGui::Text("Max: %.2f", *snapshot->definition.max_value);
            ImGui::EndTooltip();
          }
        }
      }

      if (selected && cursor_moved_by_keyboard && !ImGui::IsItemVisible()) {
        ImGui::SetScrollHereY(moved_up ? 0.0F : 1.0F);
      }
      if (!symbol.help.empty()) {
        ImGui::TextDisabled("    %s", symbol.help.c_str());
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  if (execute_selected) {
    const auto& current
      = results[static_cast<size_t>(state.PaletteCursor())].symbol;
    std::string command_line = current.token;
    if (!arguments.empty()) {
      command_line += " " + arguments;
    }
    (void)console.Execute(command_line);
    state.SetConsoleVisible(true);
    state.SetPaletteVisible(false);
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace oxygen::imgui::consoleui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
