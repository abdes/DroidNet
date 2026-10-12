//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <imgui.h>

#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>
#include <Oxygen/ImGui/Widgets/Toolbar.h>

namespace oxygen::imgui {

namespace {

  auto Lowered(const std::string_view text) -> std::string
  {
    std::string lowered(text);
    std::ranges::transform(
      lowered, lowered.begin(), [](const unsigned char c) -> char {
        return static_cast<char>(std::tolower(c));
      });
    return lowered;
  }

  auto WithIcon(const std::string_view icon, const std::string_view label)
    -> std::string
  {
    if (icon.empty() || label.empty()) {
      return std::string(icon.empty() ? label : icon);
    }
    std::string text(icon);
    text += "  ";
    text += label;
    return text;
  }

  auto ItemTooltip(const std::string_view text) -> void
  {
    if (text.empty()
      || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      return;
    }
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(
      std::to_address(text.begin()), std::to_address(text.end()));
    ImGui::EndTooltip();
  }

  //! Draws a clickable item and runs its action when clicked.
  auto LeafItem(const MenuItem& item) -> void
  {
    const auto label = WithIcon(item.icon, item.label);
    const bool selected = item.kind != MenuItem::Kind::kAction && item.checked;
    if (ImGui::MenuItem(label.c_str(),
          item.shortcut.empty() ? nullptr : item.shortcut.c_str(), selected,
          item.enabled)
      && item.action) {
      item.action();
    }
    ItemTooltip(item.tooltip);
  }

  //! While searching, sections and separators are hidden and submenus are
  //! flattened into the matches.
  auto DrawItems(std::span<const MenuItem> items, const std::string& filter)
    -> void
  {
    const bool searching = !filter.empty();
    // Items are rebuilt every frame, so they are identified by position:
    // an address-based id would change each frame and reopen submenus.
    int index = 0;
    for (const auto& item : items) {
      ImGui::PushID(index++);
      switch (item.kind) {
      case MenuItem::Kind::kSection:
        if (!searching) {
          ImGui::SeparatorText(item.label.c_str());
        }
        break;
      case MenuItem::Kind::kSeparator:
        if (!searching) {
          ImGui::Separator();
        }
        break;
      case MenuItem::Kind::kSubmenu:
        if (searching) {
          DrawItems(item.children, filter);
        } else if (ImGui::BeginMenu(
                     WithIcon(item.icon, item.label).c_str(), item.enabled)) {
          DrawItems(item.children, filter);
          ImGui::EndMenu();
        }
        break;
      case MenuItem::Kind::kAction:
      case MenuItem::Kind::kCheck:
      case MenuItem::Kind::kRadio:
        if (!searching || Lowered(item.label).contains(filter)) {
          LeafItem(item);
        }
        break;
      }
      ImGui::PopID();
    }
  }

} // namespace

auto DropdownMenu(const std::string_view id, const DropdownButton& button,
  const std::span<const MenuItem> items, DropdownMenuState& state,
  const bool searchable) -> bool
{
  ImGui::PushID(std::to_address(id.begin()), std::to_address(id.end()));
  auto text = WithIcon(button.icon, button.label);
  text += " " ICON_FA_CARET_DOWN;
  if (ImGui::Button(text.c_str())) {
    state.filter.front() = '\0';
    ImGui::OpenPopup("menu");
  }
  ItemTooltip(button.tooltip);

  bool open = false;
  if (ImGui::BeginPopup("menu")) {
    open = true;
    if (searchable) {
      if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SetNextItemWidth(-1.0F);
      ImGui::InputTextWithHint("##search", ICON_FA_MAGNIFYING_GLASS " Search",
        state.filter.data(), state.filter.size());
    }
    DrawItems(items, Lowered(state.filter.data()));
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return open;
}

auto ToolbarButton(
  const std::string_view id, const ToolbarButtonOptions& options) -> bool
{
  ImGui::PushID(std::to_address(id.begin()), std::to_address(id.end()));
  std::string text(options.icon);
  if (options.dropdown_arrow) {
    text += " " ICON_FA_CARET_DOWN;
  }
  ImGui::BeginDisabled(!options.enabled);
  if (options.active) {
    ImGui::PushStyleColor(
      ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  const bool clicked = ImGui::Button(text.c_str());
  if (options.active) {
    ImGui::PopStyleColor();
  }
  ImGui::EndDisabled();
  ItemTooltip(options.tooltip);
  ImGui::PopID();
  return clicked;
}

auto PlayControls(const PlayControlsState& state) -> PlayAction
{
  struct Button {
    std::string_view id;
    std::string_view icon;
    std::string_view tooltip;
    bool enabled;
    PlayAction action;
  };
  const bool can_begin = state.sessions_available && !state.session_running;
  const std::array buttons {
    Button {
      .id = "play",
      .icon = ICON_FA_PLAY,
      .tooltip = state.paused ? "Resume" : "Play",
      .enabled = state.paused || can_begin,
      .action = PlayAction::kPlay,
    },
    Button {
      .id = "pause",
      .icon = ICON_FA_PAUSE,
      .tooltip = "Pause",
      .enabled = !state.paused,
      .action = PlayAction::kPause,
    },
    Button {
      .id = "step",
      .icon = ICON_FA_FORWARD_STEP,
      .tooltip = "Step one frame",
      .enabled = state.paused,
      .action = PlayAction::kStep,
    },
    Button {
      .id = "stop",
      .icon = ICON_FA_STOP,
      .tooltip = "Stop",
      .enabled = state.session_running,
      .action = PlayAction::kStop,
    },
  };
  const auto shown = std::span(buttons).first(
    state.sessions_available ? buttons.size() : buttons.size() - 1);

  // Centre the group in the window, never left of where the line is now.
  const auto& style = ImGui::GetStyle();
  float width = 0.0F;
  for (const auto& button : shown) {
    width += ImGui::CalcTextSize(std::to_address(button.icon.begin()),
               std::to_address(button.icon.end()))
               .x
      + (style.FramePadding.x * 2.0F);
  }
  width += style.ItemSpacing.x * static_cast<float>(shown.size() - 1);
  ImGui::SetCursorPosX(
    std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5F));

  auto result = PlayAction::kNone;
  for (std::size_t i = 0; i < shown.size(); ++i) {
    const auto& button = shown[i];
    if (i > 0) {
      ImGui::SameLine();
    }
    if (ToolbarButton(button.id,
          {
            .icon = button.icon,
            .tooltip = button.tooltip,
            .enabled = button.enabled,
            .active = false,
            .dropdown_arrow = false,
          })) {
      result = button.action;
    }
  }
  return result;
}

} // namespace oxygen::imgui
