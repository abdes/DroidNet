//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/ImGui/Widgets/GpuWorkChart.h>

namespace oxygen::imgui {

namespace {

  constexpr float kBarHeight = 14.0F;
  constexpr float kSwatchSize = 10.0F;
  constexpr float kSparklineHeight = 36.0F;
  //! Head room past the larger of the total and the budget.
  constexpr float kScaleHeadroom = 1.05F;
  constexpr ImU32 kBudgetColor = IM_COL32(255, 255, 255, 230);
  constexpr ImU32 kBarBackground = IM_COL32(40, 40, 40, 200);
  constexpr ImVec4 kHeadingColor { 0.6F, 0.6F, 0.6F, 1.0F };
  constexpr ImVec4 kFooterColor { 0.85F, 0.85F, 0.85F, 1.0F };
  constexpr ImU32 kSparklineColor = IM_COL32(110, 180, 255, 255);

  //! ImGui flags are plain int enumerators; combine them unsigned.
  template <typename... Flag> constexpr auto Combine(Flag... flags) -> int
  {
    return static_cast<int>((static_cast<unsigned>(flags) | ...));
  }

  constexpr auto kTableFlags = Combine(ImGuiTableFlags_SizingFixedFit,
    ImGuiTableFlags_NoSavedSettings, ImGuiTableFlags_RowBg);

  auto ToImColor(const std::uint32_t rgb) -> ImU32
  {
    constexpr std::uint32_t kByte = 0xFFU;
    return IM_COL32(
      (rgb >> 16U) & kByte, (rgb >> 8U) & kByte, rgb & kByte, 255);
  }

  auto Percent(const float part, const float whole) -> float
  {
    constexpr float kHundred = 100.0F;
    return whole > 0.0F ? kHundred * part / whole : 0.0F;
  }

  auto DrawBar(const GpuWorkChartRow& row, const float budget_ms) -> void
  {
    const auto width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    const auto scale = std::max(
      std::max(row.total.avg_ms, budget_ms) * kScaleHeadroom, 1.0e-3F);
    auto* draw = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    draw->AddRectFilled(origin,
      ImVec2 { origin.x + width, origin.y + kBarHeight }, kBarBackground);

    auto x = origin.x;
    const GpuWorkChartSegment* hovered = nullptr;
    const auto mouse = ImGui::GetIO().MousePos;
    for (const auto& segment : row.segments) {
      const auto extent = width * segment.cost.avg_ms / scale;
      if (extent <= 0.0F) {
        continue;
      }
      const auto end = std::min(x + extent, origin.x + width);
      draw->AddRectFilled(ImVec2 { x, origin.y },
        ImVec2 { end, origin.y + kBarHeight }, ToImColor(segment.color_rgb));
      if (mouse.x >= x && mouse.x < end && mouse.y >= origin.y
        && mouse.y < origin.y + kBarHeight) {
        hovered = &segment;
      }
      x = end;
    }
    if (budget_ms > 0.0F) {
      const auto marker = origin.x + width * budget_ms / scale;
      draw->AddLine(ImVec2 { marker, origin.y - 2.0F },
        ImVec2 { marker, origin.y + kBarHeight + 2.0F }, kBudgetColor, 2.0F);
    }
    ImGui::Dummy(ImVec2 { width, kBarHeight });
    if (hovered != nullptr && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s: %.3f ms avg, %.3f ms max (%.0f%%)",
        hovered->label.c_str(), hovered->cost.avg_ms, hovered->cost.max_ms,
        Percent(hovered->cost.avg_ms, row.total.avg_ms));
    }
  }

  auto DrawSwatch(const std::uint32_t rgb) -> void
  {
    const auto origin = ImGui::GetCursorScreenPos();
    const auto offset = (ImGui::GetTextLineHeight() - kSwatchSize) * 0.5F;
    ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2 { origin.x, origin.y + offset },
      ImVec2 { origin.x + kSwatchSize, origin.y + offset + kSwatchSize },
      ToImColor(rgb));
    ImGui::Dummy(ImVec2 { kSwatchSize, ImGui::GetTextLineHeight() });
    ImGui::SameLine();
  }

  auto CostColumns(const GpuWorkChartCost& cost, const float total) -> void
  {
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", cost.avg_ms);
    ImGui::TableNextColumn();
    ImGui::Text("%.0f%%", Percent(cost.avg_ms, total));
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", cost.max_ms);
  }

  //! Wide enough for every label with its swatch, expander and indent, so no
  //! label is clipped.
  auto LabelColumnWidth(const GpuWorkChartRow& row) -> float
  {
    const auto& style = ImGui::GetStyle();
    auto width = ImGui::CalcTextSize("Work").x;
    for (const auto& segment : row.segments) {
      width = std::max(width, ImGui::CalcTextSize(segment.label.c_str()).x);
      for (const auto& item : segment.items) {
        width = std::max(width,
          ImGui::CalcTextSize(item.label.c_str()).x + style.IndentSpacing);
      }
    }
    return width + kSwatchSize + style.ItemSpacing.x
      + ImGui::GetTreeNodeToLabelSpacing();
  }

  auto DrawLegend(const GpuWorkChartRow& row) -> void
  {
    constexpr int kColumns = 4;
    if (!ImGui::BeginTable("##legend", kColumns, kTableFlags)) {
      return;
    }
    const auto number_width = ImGui::CalcTextSize("000.000").x;
    ImGui::TableSetupColumn(
      "Work", ImGuiTableColumnFlags_WidthFixed, LabelColumnWidth(row));
    ImGui::TableSetupColumn(
      "avg ms", ImGuiTableColumnFlags_WidthFixed, number_width);
    ImGui::TableSetupColumn(
      "%", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("100%").x);
    ImGui::TableSetupColumn(
      "max ms", ImGuiTableColumnFlags_WidthFixed, number_width);
    ImGui::TableNextRow();
    for (const auto* heading : { "Work", "avg ms", "%", "max ms" }) {
      ImGui::TableNextColumn();
      ImGui::TextColored(kHeadingColor, "%s", heading);
    }
    // The bar's order, and IDs by label: rows never move under the cursor and
    // an expanded entry stays expanded when costs change.
    for (const auto& segment : row.segments) {
      ImGui::PushID(segment.label.c_str());
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      DrawSwatch(segment.color_rgb);
      // The whole row highlights; leaves keep the expander's indent so
      // labels line up.
      const auto open = ImGui::TreeNodeEx("##segment",
        Combine(ImGuiTreeNodeFlags_SpanAllColumns,
          ImGuiTreeNodeFlags_NoTreePushOnOpen,
          segment.items.empty() ? ImGuiTreeNodeFlags_Leaf
                                : ImGuiTreeNodeFlags_None),
        "%s", segment.label.c_str());
      if (!segment.tooltip.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", segment.tooltip.c_str());
      }
      CostColumns(segment.cost, row.total.avg_ms);
      if (open && !segment.items.empty()) {
        for (const auto& item : segment.items) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::Indent();
          ImGui::TextUnformatted(item.label.c_str());
          ImGui::Unindent();
          CostColumns(item.cost, row.total.avg_ms);
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  //! "GPU frame" with the latest and peak values on a line of their own, then
  //! the graph: one line, the budget dashed across it, the latest sample
  //! dotted. No text covers the graph.
  auto DrawSparkline(
    const std::span<const float> history, const float budget_ms) -> void
  {
    if (history.empty()) {
      return;
    }
    const auto peak = *std::ranges::max_element(history);
    const auto values
      = fmt::format("now {:.2f} ms   peak {:.2f} ms", history.back(), peak);
    ImGui::TextColored(kHeadingColor, "GPU frame");
    ImGui::SameLine(
      ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(values.c_str()).x);
    ImGui::TextUnformatted(values.c_str());

    const auto width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    const auto top
      = std::max(std::max(peak, budget_ms) * kScaleHeadroom, 1.0e-3F);
    auto* draw = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto bottom = origin.y + kSparklineHeight;
    draw->AddRectFilled(
      origin, ImVec2 { origin.x + width, bottom }, kBarBackground);
    const auto y_of = [&](const float ms) -> float {
      return bottom - (kSparklineHeight * std::min(ms, top) / top);
    };
    if (budget_ms > 0.0F) {
      constexpr float kDash = 4.0F;
      const auto y = y_of(budget_ms);
      for (auto x = origin.x; x < origin.x + width; x += 2.0F * kDash) {
        draw->AddLine(ImVec2 { x, y },
          ImVec2 { std::min(x + kDash, origin.x + width), y }, kBudgetColor);
      }
    }
    const auto step = history.size() > 1U
      ? width / static_cast<float>(history.size() - 1U)
      : 0.0F;
    auto points = std::vector<ImVec2> {};
    points.reserve(history.size());
    for (const auto [index, ms] : std::views::enumerate(history)) {
      points.push_back(
        ImVec2 { origin.x + (step * static_cast<float>(index)), y_of(ms) });
    }
    draw->AddPolyline(points.data(), static_cast<int>(points.size()),
      kSparklineColor, ImDrawFlags_None, 1.5F);
    draw->AddCircleFilled(points.back(), 2.5F, kSparklineColor);
    ImGui::Dummy(ImVec2 { width, kSparklineHeight });
  }

} // namespace

auto DrawGpuWorkChart(const GpuWorkChartData& data) -> void
{
  for (const auto& row : data.rows) {
    ImGui::PushID(row.label.c_str());
    const auto flags = Combine(ImGuiTreeNodeFlags_SpanFullWidth,
      row.default_open ? ImGuiTreeNodeFlags_DefaultOpen
                       : ImGuiTreeNodeFlags_None);
    const auto open
      = ImGui::TreeNodeEx("##row", flags, "%s  %.2f ms (max %.2f)",
        row.label.c_str(), row.total.avg_ms, row.total.max_ms);
    DrawBar(row, data.budget_ms);
    if (open) {
      DrawLegend(row);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  DrawSparkline(data.history_ms, data.budget_ms);
  if (!data.footer.empty()) {
    ImGui::TextColored(kFooterColor, "%s", data.footer.c_str());
  }
}

} // namespace oxygen::imgui
