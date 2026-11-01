//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/ContentPanelWidgets.h"
#include <imgui.h>
#include <imgui_internal.h>

#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise) -
// ImGui varargs and int flag enumerators

namespace oxygen::examples::ui::content_widgets {

namespace {
  //! DemoShell's font size, at which the mockup's units are pixels.
  constexpr auto kBaseFontSize = 16.0F;
  //! The mockup's small text over its regular text.
  constexpr auto kSmallRatio = 11.0F / 12.0F;
  //! Where property values start, from the label.
  constexpr auto kValueColumn = 96.0F;
  //! Where a group's hint starts, as a share of the width.
  constexpr auto kHintColumn = 0.56F;
  constexpr auto kDotRadius = 4.0F;
  constexpr auto kCalloutPadding = 8.0F;
  constexpr auto kRowTextInset = 8.0F;

  auto HeaderKey(const std::string_view section) -> std::string
  {
    return "demo_shell.panels.content." + std::string(section) + ".open";
  }

  auto Color(const ImVec4& color) -> ImU32 { return ImGui::GetColorU32(color); }

  auto SmallSize() -> float { return ImGui::GetFontSize() * kSmallRatio; }

  //! The begin and end of `text`, for ImGui's begin and end pointer pairs.
  auto TextBegin(const std::string_view text) -> const char*
  {
    return std::to_address(text.begin());
  }

  auto TextEnd(const std::string_view text) -> const char*
  {
    return std::to_address(text.end());
  }

  //! Draws `text` with the window's draw list, in the small size when
  //! `small`.
  auto DrawText(const ImVec2 position, const ImVec4& color,
    const std::string_view text, const bool small) -> void
  {
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),
      small ? SmallSize() : ImGui::GetFontSize(), position, Color(color),
      TextBegin(text), TextEnd(text));
  }

  //! The gutter on each side of a scrolling region's content.
  constexpr auto kGutter = 10.0F;
  //! The space each side of a toggle's text.
  constexpr auto kTogglePadding = 9.0F;
  //! The scrollbar's width, within the gutter.
  constexpr auto kScrollbar = 5.0F;

  //! A scrolling child: its size, the padding each side of its content and
  //! its child flags.
  struct ScrollChild final {
    const char* id { nullptr };
    ImVec2 size;
    float padding { 0.0F };
    ImGuiChildFlags flags { ImGuiChildFlags_None };
  };

  //! Opens a scrolling child with `padding` each side of its content, and
  //! ends its content `padding` (at least the scrollbar's width) from its
  //! right edge whether or not the scrollbar shows: the scrollbar sits in the
  //! gutter instead of narrowing the content.
  auto BeginScrollChild(const ScrollChild& child) -> bool
  {
    const auto& [id, size, padding, flags] = child;
    const float unit = ImGui::GetFontSize() / kBaseFontSize;
    const float scrollbar = kScrollbar * unit;
    const bool framed = (flags & ImGuiChildFlags_FrameStyle) != 0;
    // A framed child takes its padding from the frame padding.
    ImGui::PushStyleVar(
      framed ? ImGuiStyleVar_FramePadding : ImGuiStyleVar_WindowPadding,
      ImVec2 { padding, framed ? 2.0F * unit : 0.0F });
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, scrollbar);
    const bool open = ImGui::BeginChild(
      id, size, flags | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    auto* window = ImGui::GetCurrentWindow();
    const float right
      = window->Pos.x + window->Size.x - std::max(padding, scrollbar);
    window->WorkRect.Max.x = right;
    window->ContentRegionRect.Max.x = right;
    return open;
  }

  auto TextWidth(const std::string_view text, const bool small) -> float
  {
    const float size = small ? SmallSize() : ImGui::GetFontSize();
    return ImGui::GetFont()
      ->CalcTextSizeA(size, FLT_MAX, 0.0F, TextBegin(text), TextEnd(text))
      .x;
  }
} // namespace

auto Unit() -> float { return ImGui::GetFontSize() / kBaseFontSize; }

auto Dim() -> ImVec4 { return ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]; }

SmallFont::SmallFont()
{
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kSmallRatio);
}

SmallFont::~SmallFont() { ImGui::PopFont(); }

auto DimSmall(const std::string_view text) -> void
{
  const auto font = SmallFont {};
  ImGui::PushStyleColor(ImGuiCol_Text, Dim());
  ImGui::TextUnformatted(TextBegin(text), TextEnd(text));
  ImGui::PopStyleColor();
}

auto AlignRight(const float width) -> void
{
  ImGui::SameLine();
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - width));
}

auto SectionHeader(const char* label, const std::string_view section,
  const bool default_open, const std::string_view note) -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  const auto key = HeaderKey(section);
  const bool saved = settings != nullptr
    ? settings->GetBool(key).value_or(default_open)
    : default_open;
  ImGui::SetNextItemOpen(saved, ImGuiCond_Always);
  // Overlap lets a band hold a link, such as Import settings' Reset.
  const bool open
    = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_AllowOverlap);
  if (!note.empty()) {
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    const float width = TextWidth(note, true);
    const float x = max.x - width - (kRowTextInset * Unit());
    const float y = min.y + (((max.y - min.y) - SmallSize()) * 0.5F);
    DrawText({ x, y }, ImGui::GetStyle().Colors[ImGuiCol_Text], note, true);
  }
  if (open != saved && settings != nullptr) {
    settings->SetBool(key, open);
  }
  return open;
}

auto GroupHeader(
  const char* id, const std::string_view label, const GroupNode& node) -> bool
{
  const auto text = std::string(label);
  const auto flags = ImGuiTreeNodeFlags_SpanAvailWidth
    | (node.default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
  const float start = ImGui::GetCursorScreenPos().x;
  const bool open = ImGui::TreeNodeEx(id, flags, "%s", text.c_str());
  const auto min = ImGui::GetItemRectMin();
  const auto max = ImGui::GetItemRectMax();
  const float y = min.y + (((max.y - min.y) - ImGui::GetFontSize()) * 0.5F);
  float x = start + ImGui::GetTreeNodeToLabelSpacing() + TextWidth(label, false)
    + (4.0F * Unit());
  if (node.changed) {
    DrawDot({ x + (2.0F * Unit()), (min.y + max.y) * 0.5F }, kAmber);
    x += 8.0F * Unit();
  }
  if (!open && node.count) {
    const auto count = "(" + std::to_string(*node.count) + ")";
    DrawText({ x, y }, Dim(), count, false);
  }
  if (!node.hint.empty()) {
    const float hint_x = start + ((max.x - start) * kHintColumn);
    const float small_y = min.y + (((max.y - min.y) - SmallSize()) * 0.5F);
    DrawText({ hint_x, small_y }, Dim(), node.hint, true);
  }
  return open;
}

auto FieldLabel(const char* label, const bool changed) -> void
{
  {
    const auto font = SmallFont {};
    ImGui::PushStyleColor(ImGuiCol_Text, Dim());
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
  }
  if (changed) {
    const auto at = ImGui::GetCursorScreenPos();
    DrawDot({ at.x - (8.0F * Unit()), at.y + (ImGui::GetFrameHeight() * 0.5F) },
      kAmber);
  }
  // In a field pair, the pair has set the width.
  if (ImGui::GetCurrentWindow()->DC.ItemWidthStack.empty()) {
    ImGui::SetNextItemWidth(-1.0F);
  }
}

auto FieldPair::Next() -> void
{
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  if (!open_) {
    open_ = true;
    right_ = false;
    const float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5F;
    ImGui::BeginGroup();
    ImGui::PushItemWidth(width);
    return;
  }
  const float width = ImGui::CalcItemWidth();
  ImGui::PopItemWidth();
  ImGui::EndGroup();
  if (!right_) {
    ImGui::SameLine();
    right_ = true;
  } else {
    right_ = false;
  }
  ImGui::BeginGroup();
  ImGui::PushItemWidth(width);
}

FieldPair::~FieldPair()
{
  if (open_) {
    ImGui::PopItemWidth();
    ImGui::EndGroup();
  }
}

auto CommandCheckbox(const PanelCommands& commands, const std::string_view name,
  const char* label, bool value) -> void
{
  if (ImGui::Checkbox(label, &value)) {
    commands.RunBool(name, value);
  }
}

auto CommandDragFloat(const PanelCommands& commands,
  const std::string_view name, const char* label, float value,
  const float speed, const float minimum, const float maximum,
  const bool changed) -> void
{
  ImGui::PushID(label);
  FieldLabel(label, changed);
  if (ImGui::DragFloat("##value", &value, speed, minimum, maximum)) {
    commands.RunFloat(name, value);
  }
  ImGui::PopID();
}

auto CommandDragInt(const PanelCommands& commands, const std::string_view name,
  const char* label, int value, const float speed, const int minimum,
  const int maximum, const bool changed) -> void
{
  ImGui::PushID(label);
  FieldLabel(label, changed);
  if (ImGui::DragInt("##value", &value, speed, minimum, maximum)) {
    commands.RunInt(name, value);
  }
  ImGui::PopID();
}

auto SearchField(const char* id, const char* hint, std::string& value,
  const float width) -> bool
{
  // ImGui edits a fixed buffer; the text ends at its first NUL.
  auto buffer = std::array<char, 256> {};
  std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
  const auto& style = ImGui::GetStyle();
  const float icon_width = TextWidth(ICON_FA_MAGNIFYING_GLASS, false);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
    { style.FramePadding.x + icon_width + (6.0F * Unit()),
      style.FramePadding.y });
  ImGui::SetNextItemWidth(width);
  const bool edited
    = ImGui::InputTextWithHint(id, hint, buffer.data(), buffer.size());
  ImGui::PopStyleVar();
  const auto min = ImGui::GetItemRectMin();
  DrawText({ min.x + style.FramePadding.x, min.y + style.FramePadding.y },
    Dim(), ICON_FA_MAGNIFYING_GLASS, false);
  if (!edited) {
    return false;
  }
  value = buffer.data();
  return true;
}

auto Chip(const char* label, const bool active) -> bool
{
  const auto& style = ImGui::GetStyle();
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight());
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
    { 10.0F * Unit(), style.FramePadding.y * 0.7F });
  const auto& fill = active ? kChipOn : kChipOff;
  ImGui::PushStyleColor(ImGuiCol_Button, fill);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
    active ? style.Colors[ImGuiCol_ButtonHovered] : kChipOn);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kPrimary);
  ImGui::PushStyleColor(
    ImGuiCol_Text, active ? kWhite : style.Colors[ImGuiCol_Text]);
  const bool clicked = ImGui::Button(label);
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  return clicked;
}

auto ToggleGroupWidth(const std::span<const Toggle> toggles) -> float
{
  float width = 0.0F;
  for (const auto& toggle : toggles) {
    width += ImGui::CalcTextSize(toggle.label, nullptr, true).x
      + (2.0F * kTogglePadding * Unit());
  }
  return width;
}

auto ToggleGroup(const char* id, const std::span<const Toggle> toggles)
  -> std::optional<std::size_t>
{
  const auto& style = ImGui::GetStyle();
  const float height = ImGui::GetFrameHeight();
  const float rounding = style.FrameRounding;
  const auto start = ImGui::GetCursorScreenPos();
  const float width = ToggleGroupWidth(toggles);
  auto* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(
    start, { start.x + width, start.y + height }, Color(kChipOff), rounding);
  auto clicked = std::optional<std::size_t> {};
  ImGui::PushID(id);
  float x = start.x;
  for (std::size_t index = 0; index < toggles.size(); ++index) {
    const auto& toggle = toggles[index];
    const auto text = ImGui::CalcTextSize(toggle.label, nullptr, true);
    const float segment = text.x + (2.0F * kTogglePadding * Unit());
    ImGui::SetCursorScreenPos({ x, start.y });
    if (ImGui::InvisibleButton(toggle.label, { segment, height })) {
      clicked = index;
    }
    const bool hovered = ImGui::IsItemHovered();
    // Only the group's outer corners are rounded.
    ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
    if (index == 0) {
      corners |= ImDrawFlags_RoundCornersLeft;
    }
    if (index + 1 == toggles.size()) {
      corners |= ImDrawFlags_RoundCornersRight;
    }
    if (toggle.on || hovered) {
      auto fill = style.Colors[ImGuiCol_FrameBgHovered];
      if (toggle.on) {
        fill = hovered ? style.Colors[ImGuiCol_ButtonHovered] : kChipOn;
      }
      draw_list->AddRectFilled({ x, start.y },
        { x + segment, start.y + height }, Color(fill), rounding, corners);
    }
    if (index > 0) {
      draw_list->AddLine({ x, start.y + (3.0F * Unit()) },
        { x, start.y + height - (3.0F * Unit()) },
        Color(style.Colors[ImGuiCol_Border]));
    }
    const auto* end = ImGui::FindRenderedTextEnd(toggle.label);
    draw_list->AddText(
      { x + ((segment - text.x) * 0.5F), start.y + ((height - text.y) * 0.5F) },
      Color(toggle.on ? kWhite : style.Colors[ImGuiCol_Text]), toggle.label,
      end);
    x += segment;
  }
  ImGui::PopID();
  // The group is one item for the layout.
  ImGui::SetCursorScreenPos(start);
  ImGui::Dummy({ width, height });
  return clicked;
}

auto PrimaryButton(const char* label) -> bool
{
  ImGui::PushStyleColor(ImGuiCol_Button, kPrimary);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
    ImVec4 { kPrimary.x * 1.15F, kPrimary.y * 1.15F, kPrimary.z, 1.0F });
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kChipOn);
  ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
  const bool clicked = ImGui::Button(label);
  ImGui::PopStyleColor(4);
  return clicked;
}

auto DrawDot(const ImVec2 center, const ImVec4& color, const bool filled)
  -> void
{
  auto* draw_list = ImGui::GetWindowDrawList();
  const float radius = kDotRadius * Unit();
  if (filled) {
    draw_list->AddCircleFilled(center, radius, Color(color));
  } else {
    draw_list->AddCircle(center, radius, Color(color), 0, Unit());
  }
}

auto Dot(const ImVec4& color, const bool filled) -> void
{
  const auto at = ImGui::GetCursorScreenPos();
  const float size = ImGui::GetTextLineHeight();
  DrawDot({ at.x + (size * 0.5F), at.y + (size * 0.5F) }, color, filled);
  ImGui::Dummy({ size, size });
}

auto ItemTooltip(const std::string_view text) -> void
{
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    const auto tooltip = std::string(text);
    ImGui::SetTooltip("%s", tooltip.c_str());
  }
}

auto Elide(const std::string_view text, const float width) -> std::string
{
  if (ImGui::CalcTextSize(TextBegin(text), TextEnd(text)).x <= width) {
    return std::string(text);
  }
  constexpr auto kEllipsis = std::string_view { "\xE2\x80\xA6" };
  auto head = text.size() / 2;
  auto tail = text.size() - head;
  auto result = std::string {};
  while (head > 0 || tail > 0) {
    if (head >= tail && head > 0) {
      --head;
    } else if (tail > 0) {
      --tail;
    }
    result = std::string(text.substr(0, head)) + std::string(kEllipsis)
      + std::string(text.substr(text.size() - tail));
    if (ImGui::CalcTextSize(result.c_str()).x <= width) {
      return result;
    }
  }
  return std::string(kEllipsis);
}

auto PropertyRow(const char* label, const std::string_view value) -> void
{
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", label);
  ImGui::SameLine(kValueColumn * Unit());
  const auto shown = Elide(value, ImGui::GetContentRegionAvail().x);
  ImGui::TextUnformatted(shown.c_str());
  if (shown != value) {
    ItemTooltip(value);
  }
  ImGui::PopID();
}

auto ListRow(const char* id, const bool selected, const float lines) -> Row
{
  const float line = 20.0F * Unit();
  const float height = (line * lines) - ImGui::GetStyle().ItemSpacing.y;
  // The highlight spans the full width, as the mockup's rows do; the text
  // keeps the tree's indent.
  const auto* window = ImGui::GetCurrentWindow();
  const auto at = ImGui::GetCursorScreenPos();
  const float indent = at.x - window->WorkRect.Min.x;
  ImGui::SetCursorScreenPos({ window->WorkRect.Min.x, at.y });
  auto row = Row { .line_height = line, .indent = indent };
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0.0F, 0.5F });
  row.clicked = ImGui::Selectable(id, selected,
    ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap
      | ImGuiSelectableFlags_SpanAvailWidth,
    { 0.0F, height });
  ImGui::PopStyleVar();
  row.hovered = ImGui::IsItemHovered();
  row.double_clicked
    = row.clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  const auto max = ImGui::GetItemRectMax();
  const auto min = ImGui::GetItemRectMin();
  row.origin = { min.x + indent, min.y };
  row.size = { max.x - row.origin.x, max.y - row.origin.y };
  return row;
}

auto RowLineCenter(const Row& row, const float line) -> float
{
  // The selectable gives up the item spacing below its last line.
  const float top_line = row.line_height - ImGui::GetStyle().ItemSpacing.y;
  return row.origin.y + (top_line * 0.5F) + (row.line_height * line);
}

auto RowText(const Row& row, const float offset, const std::string_view text,
  const RowTextStyle& style) -> void
{
  const float size = style.small ? SmallSize() : ImGui::GetFontSize();
  const float y = RowLineCenter(row, style.line) - (size * 0.5F);
  const float x = style.right
    ? row.origin.x + row.size.x - offset - TextWidth(text, style.small)
    : row.origin.x + offset;
  const auto color = style.color.w > 0.0F
    ? style.color
    : ImGui::GetStyle().Colors[ImGuiCol_Text];
  DrawText({ x, y }, color, text, style.small);
}

auto RowDot(const Row& row, const float offset, const ImVec4& color,
  const bool filled, const float line) -> void
{
  DrawDot({ row.origin.x + offset, RowLineCenter(row, line) }, color, filled);
}

Callout::Callout(const CalloutKind kind)
  : start_(ImGui::GetCursorScreenPos())
  , width_(ImGui::GetContentRegionAvail().x)
  , fill_(kind == CalloutKind::kError ? kErrorFill : kWarningFill)
  , border_(kind == CalloutKind::kError ? kRed : kAmber)
{
  auto* draw_list = ImGui::GetWindowDrawList();
  draw_list->ChannelsSplit(2);
  draw_list->ChannelsSetCurrent(1);
  const float padding = kCalloutPadding * Unit();
  ImGui::BeginGroup();
  ImGui::Dummy({ 0.0F, padding * 0.25F });
  ImGui::Indent(padding);
  ImGui::PushTextWrapPos(start_.x - ImGui::GetWindowPos().x + width_ - padding);
}

Callout::~Callout()
{
  const float padding = kCalloutPadding * Unit();
  ImGui::PopTextWrapPos();
  ImGui::Unindent(padding);
  ImGui::Dummy({ 0.0F, padding * 0.25F });
  ImGui::EndGroup();
  auto* draw_list = ImGui::GetWindowDrawList();
  draw_list->ChannelsSetCurrent(0);
  const auto end = ImVec2 { start_.x + width_, ImGui::GetItemRectMax().y };
  const float rounding = 3.0F * Unit();
  draw_list->AddRectFilled(start_, end, Color(fill_), rounding);
  draw_list->AddRect(start_, end, Color(border_), rounding);
  draw_list->ChannelsMerge();
}

auto BeginTabBody(const char* id, const float height) -> bool
{
  return BeginScrollChild({
    .id = id,
    .size = { 0.0F, height },
    .padding = kGutter * Unit(),
  });
}

auto BeginScrollList(const char* id, const float height) -> bool
{
  // It reaches into the tab body's right gutter by the scrollbar's width,
  // which the body still shows: its clipping stops at its edge.
  const float width = ImGui::GetContentRegionAvail().x + (kScrollbar * Unit());
  return BeginScrollChild({ .id = id, .size = { width, height } });
}

auto EndScrollArea() -> void { ImGui::EndChild(); }

auto BeginFramedList(const char* id, const float height) -> bool
{
  return BeginScrollChild({
    .id = id,
    .size = { 0.0F, height },
    .padding = kGutter * Unit(),
    .flags = ImGuiChildFlags_FrameStyle,
  });
}

auto EndFramedList() -> void { ImGui::EndChild(); }

auto BeginFramedBlock(const char* id) -> bool
{
  return BeginScrollChild({
    .id = id,
    .size = { 0.0F, 0.0F },
    .padding = kGutter * Unit(),
    .flags = ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY,
  });
}

auto EndFramedBlock() -> void { ImGui::EndChild(); }

} // namespace oxygen::examples::ui::content_widgets

// NOLINTEND(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise)
