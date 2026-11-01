//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/ContentImportProfile.h"
#include <imgui.h>

#include <Oxygen/Base/NoStd.h>

//! The content panel's controls, as its mockup draws them: bands with a
//! right-hand note, label-above fields, chips, state dots, framed
//! lists, two-column property rows and callout boxes.
/*!
 Sizes are in mockup units (`Unit()`): the mockup measures DemoShell's
 controls in pixels at its 16 px font (22 px frames, 20 px rows), so one unit
 is the current font size over 16. Its text is drawn at 12 px and its small
 text at 11 px; small text keeps that ratio to the font.
*/
namespace oxygen::examples::ui::content_widgets {

inline constexpr auto kGreen = ImVec4 { 0.298F, 0.761F, 0.478F, 1.0F };
inline constexpr auto kAmber = ImVec4 { 0.910F, 0.706F, 0.290F, 1.0F };
inline constexpr auto kRed = ImVec4 { 0.898F, 0.376F, 0.353F, 1.0F };
inline constexpr auto kInfo = ImVec4 { 0.416F, 0.659F, 0.941F, 1.0F };
inline constexpr auto kErrorFill = ImVec4 { 0.227F, 0.149F, 0.161F, 1.0F };
inline constexpr auto kWarningFill = ImVec4 { 0.227F, 0.200F, 0.141F, 1.0F };
inline constexpr auto kErrorCode = ImVec4 { 0.941F, 0.639F, 0.624F, 1.0F };
inline constexpr auto kChipOn = ImVec4 { 0.227F, 0.431F, 0.659F, 1.0F };
inline constexpr auto kChipOff = ImVec4 { 0.169F, 0.204F, 0.259F, 1.0F };
inline constexpr auto kPrimary = ImVec4 { 0.180F, 0.525F, 0.910F, 1.0F };
inline constexpr auto kStatusFill = ImVec4 { 0.102F, 0.141F, 0.200F, 1.0F };
inline constexpr auto kWhite = ImVec4 { 1.0F, 1.0F, 1.0F, 1.0F };

//! One mockup unit in pixels.
[[nodiscard]] auto Unit() -> float;

//! The dimmed text color.
[[nodiscard]] auto Dim() -> ImVec4;

//! Draws in the mockup's small size (11 of 12) while it lives.
class SmallFont final {
public:
  SmallFont();
  ~SmallFont();

  SmallFont(const SmallFont&) = delete;
  auto operator=(const SmallFont&) -> SmallFont& = delete;
  SmallFont(SmallFont&&) = delete;
  auto operator=(SmallFont&&) -> SmallFont& = delete;
};

//! Dimmed small text.
auto DimSmall(std::string_view text) -> void;

//! Places the next item on this line so it ends at the right edge; `width`
//! is its width.
auto AlignRight(float width) -> void;

//! A `CollapsingHeader` band whose open state is saved as
//! `demo_shell.panels.content.<section>.open`; true while it is open.
//! `note` is drawn small at its right, as `loose root · 29 scenes`.
auto SectionHeader(const char* label, std::string_view section,
  bool default_open = true, std::string_view note = {}) -> bool;

//! A tree node of a list or a settings group, open by default. While it is
//! closed, `count` follows its label, dimmed; `hint` is drawn dimmed at the
//! hint column; `changed` puts an amber dot after the label.
struct GroupNode final {
  std::optional<std::size_t> count;
  std::string_view hint;
  bool changed { false };
  bool default_open { true };
};
auto GroupHeader(const char* id, std::string_view label, const GroupNode& node)
  -> bool;

//! The label of the next control, above it; the control then fills the width.
//! `changed` puts an amber dot left of the field.
auto FieldLabel(const char* label, bool changed = false) -> void;

//! Lays fields out two per line: `Next` starts the left or the right column
//! and sets the width of its control.
class FieldPair final {
public:
  auto Next() -> void;
  ~FieldPair();

  FieldPair() = default;
  FieldPair(const FieldPair&) = delete;
  auto operator=(const FieldPair&) -> FieldPair& = delete;
  FieldPair(FieldPair&&) = delete;
  auto operator=(FieldPair&&) -> FieldPair& = delete;

private:
  bool right_ { false };
  bool open_ { false };
};

//! A checkbox that runs the bool command `name`.
auto CommandCheckbox(const PanelCommands& commands, std::string_view name,
  const char* label, bool value) -> void;

//! A label-above drag of a float that runs the float command `name`.
auto CommandDragFloat(const PanelCommands& commands, std::string_view name,
  const char* label, float value, float speed, float minimum, float maximum,
  bool changed = false) -> void;

//! A label-above drag of an int that runs the int command `name`.
auto CommandDragInt(const PanelCommands& commands, std::string_view name,
  const char* label, int value, float speed, int minimum, int maximum,
  bool changed = false) -> void;

//! A search field with a magnifier, whose text applies as it is typed; true
//! when `value` changed. It fills the width unless `width` is positive.
auto SearchField(const char* id, const char* hint, std::string& value,
  float width = -1.0F) -> bool;

//! A rounded toggle of a chip row; true when clicked.
auto Chip(const char* label, bool active) -> bool;

//! One button of a toggle group.
struct Toggle final {
  const char* label { nullptr };
  bool on { false };
};

//! The width a toggle group of `toggles` takes.
[[nodiscard]] auto ToggleGroupWidth(std::span<const Toggle> toggles) -> float;

//! Joined toggle buttons in one frame, each sized to its text, such as the
//! format filters FBX | GLB | GLTF; returns the index of the one clicked.
auto ToggleGroup(const char* id, std::span<const Toggle> toggles)
  -> std::optional<std::size_t>;

//! A button in the primary color, such as Load.
auto PrimaryButton(const char* label) -> bool;

//! Draws a state dot centered at `center`; hollow when `filled` is false.
auto DrawDot(ImVec2 center, const ImVec4& color, bool filled = true) -> void;

//! A state dot item on the line, as tall as text.
auto Dot(const ImVec4& color, bool filled = true) -> void;

//! A tooltip for the last item that also shows while it is disabled.
auto ItemTooltip(std::string_view text) -> void;

//! `text` shortened in its middle with `…` to fit `width`.
[[nodiscard]] auto Elide(std::string_view text, float width) -> std::string;

//! A row of a property list: the dimmed label, then the value at the value
//! column, elided to one line; the full value is its tooltip.
auto PropertyRow(const char* label, std::string_view value) -> void;

//! A selectable list row of `lines` text lines whose content the caller draws
//! with `RowText`, from `Row::origin`.
struct Row final {
  bool clicked { false };
  bool double_clicked { false };
  bool hovered { false };
  //! The height of one of its text lines.
  float line_height { 0.0F };
  //! How far the tree indents the row; the row's highlight spans the full
  //! width, and its text offsets start at `origin`, after the indent.
  float indent { 0.0F };
  ImVec2 origin;
  ImVec2 size;
};
auto ListRow(const char* id, bool selected, float lines = 1.0F) -> Row;

//! Draws `text` at `offset` from a row's origin; `line` picks the text line.
//! `right` aligns its end at the row's right edge minus `offset.x`.
struct RowTextStyle final {
  ImVec4 color { 0.0F, 0.0F, 0.0F, 0.0F };
  bool small { false };
  bool right { false };
  float line { 0.0F };
};
auto RowText(const Row& row, float offset, std::string_view text,
  const RowTextStyle& style) -> void;

//! Draws a state dot in a row, centered `offset` from its left edge on
//! line `line`.
auto RowDot(const Row& row, float offset, const ImVec4& color,
  bool filled = true, float line = 0.0F) -> void;

//! The vertical center of a row's text line `line`.
[[nodiscard]] auto RowLineCenter(const Row& row, float line) -> float;

//! What a callout box says: its fill and border colors.
enum class CalloutKind : uint8_t {
  //! Amber, such as settings that differ from a record's.
  kWarning,
  //! Red, such as a refusal's findings.
  kError,
};

//! A box with a fill and a border around what is drawn while it lives, such
//! as a refusal's findings.
class Callout final {
public:
  explicit Callout(CalloutKind kind);
  ~Callout();

  Callout(const Callout&) = delete;
  auto operator=(const Callout&) -> Callout& = delete;
  Callout(Callout&&) = delete;
  auto operator=(Callout&&) -> Callout& = delete;

private:
  ImVec2 start_;
  float width_ { 0.0F };
  ImVec4 fill_;
  ImVec4 border_;
};

// Scrolling regions keep a gutter on both sides of their content; a narrow
// scrollbar shows in the right gutter, so it never covers the content nor
// narrows it.

//! A tab's scrolling body, `height` pixels high, with a gutter each side.
auto BeginTabBody(const char* id, float height) -> bool;
//! A scrolling list, `height` pixels high, aligned with the content around
//! it; its scrollbar shows in the enclosing tab body's right gutter.
auto BeginScrollList(const char* id, float height) -> bool;
//! Closes a tab body or a scroll list.
auto EndScrollArea() -> void;

//! A framed, scrolling list region of `height` pixels; `End` closes it.
auto BeginFramedList(const char* id, float height) -> bool;
auto EndFramedList() -> void;

//! A framed region as tall as its content, which never scrolls; the tab body
//! around it does.
auto BeginFramedBlock(const char* id) -> bool;
auto EndFramedBlock() -> void;

//! A label-above combo of `choices` that runs the command `name` with the
//! picked choice's label.
template <typename EnumT, std::size_t N>
auto ChoiceCombo(const PanelCommands& commands, const std::string_view name,
  const char* label, const EnumT value,
  const std::array<Choice<EnumT>, N>& choices, const bool changed = false)
  -> void
{
  ImGui::PushID(label);
  FieldLabel(label, changed);
  const auto preview = std::string(LabelOf(choices, value));
  if (ImGui::BeginCombo("##choice", preview.c_str())) {
    for (const auto& choice : choices) {
      const bool selected = choice.value == value;
      const auto text = std::string(choice.label);
      if (ImGui::Selectable(text.c_str(), selected) && !selected) {
        commands.RunText(name, nostd::to_string(choice));
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
}

} // namespace oxygen::examples::ui::content_widgets
