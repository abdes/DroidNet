//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "DemoShell/PanelRegistry.h"
#include "DemoShell/UI/PanelSideBar.h"
#include "DemoShell/UI/UiSettingsVm.h"
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Styles/Spectrum.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise) -
// ImGui

namespace oxygen::examples::ui {

namespace {

  constexpr float kSidebarWidth = 120.0F;
  constexpr float kIconSize = 24.0F;

  auto MatchesFontName(const ImFont* font, std::string_view name) -> bool
  {
    if (font == nullptr) {
      return false;
    }
    if (std::string_view(font->GetDebugName()) == name) {
      return true;
    }
    return std::ranges::any_of(
      font->Sources, [name](const ImFontConfig* config) -> bool {
        return config != nullptr
          && std::string_view(std::data(config->Name)) == name;
      });
  }

  auto FindIconFontByName(std::string_view name) -> ImFont*
  {
    const auto& io = ImGui::GetIO();
    if (!io.Fonts) {
      return nullptr;
    }

    for (ImFont* f : io.Fonts->Fonts) {
      if (MatchesFontName(f, name)) {
        return f;
      }
    }
    if (!io.Fonts->Fonts.empty()) {
      return io.Fonts->Fonts.back();
    }
    return nullptr;
  }

  auto CenterCursorForButton(const float button_size) -> void
  {
    const float window_width = ImGui::GetWindowSize().x;
    const float offset = std::max(0.0F, (window_width - button_size) * 0.5F);
    ImGui::SetCursorPosX(std::floor(offset + 0.5F));
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - color, then alpha
  auto ToSpectrumColor(const unsigned int color, const float alpha) -> ImVec4
  {
    ImVec4 result = ImGui::ColorConvertU32ToFloat4(color);
    result.w = alpha;
    return result;
  }

} // namespace

PanelSideBar::PanelSideBar(observer_ptr<PanelRegistry> panel_registry,
  observer_ptr<UiSettingsVm> ui_settings_vm)
  : panel_registry_(panel_registry)
  , ui_settings_vm_(ui_settings_vm)
{
  DCHECK_NOTNULL_F(panel_registry, "PanelSideBar requires PanelRegistry");
  DCHECK_NOTNULL_F(ui_settings_vm, "PanelSideBar requires UiSettingsVm");
}

auto PanelSideBar::Draw(const float top) -> void
{
  const auto desired_active = ui_settings_vm_->GetActivePanelName();
  if (desired_active.has_value()) {
    DCHECK_F(!desired_active->empty(), "expecting non-empty panel names");
  }

  const auto current_active = panel_registry_->GetActivePanelName();
  if (!desired_active.has_value()) {
    if (!current_active.empty()) {
      panel_registry_->ClearActivePanel();
    }
  } else if (*desired_active != current_active
    && !panel_registry_->SetActivePanelByName(*desired_active)) {
    // A saved panel this demo no longer has.
    LOG_F(INFO, "No '{}' panel; showing none", *desired_active);
    ui_settings_vm_->SetActivePanelName(std::optional<std::string> {});
  }

  const auto& io = ImGui::GetIO();
  const float height = std::max(0.0F, io.DisplaySize.y - top);

  ImGui::SetNextWindowPos(ImVec2(0.0F, top), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(kSidebarWidth, height), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.45F);

  constexpr auto kFlags = ImGuiWindowFlags_NoDecoration
    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

  if (!ImGui::Begin("DemoPanelSideBar", nullptr, kFlags)) {
    ImGui::End();
    return;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 12.0F));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 14.0F));

  constexpr float kIconButtonPadding = 16.0F;
  constexpr float kIconButtonBaseSize = kIconSize + (kIconButtonPadding * 2.0F);

  // None button
  constexpr std::string_view kSideBarIconFontName = "oxygen-icons";
  ImFont* icon_font_24 = FindIconFontByName(kSideBarIconFontName);
  if (icon_font_24) {
    ImGui::PushFont(icon_font_24, kIconSize);
  }
  const float dpi_scale = io.FontGlobalScale > 0.0F ? io.FontGlobalScale : 1.0F;
  const float icon_button_size = kIconButtonBaseSize * dpi_scale;
  const float icon_button_padding = kIconButtonPadding * dpi_scale;
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
    ImVec2(icon_button_padding, icon_button_padding));
  // Panels toggle: click active to close, click inactive to open.

  const auto active_name = panel_registry_->GetActivePanelName();

  for (const auto& entry : panel_registry_->GetPanels()) {
    const bool is_active = entry.name == active_name;

    CenterCursorForButton(icon_button_size);

    const auto icon = entry.panel->GetIcon();
    constexpr auto kDefaultIcon = imgui::icons::kIconSettings;
    const auto* icon_text = icon.empty() ? kDefaultIcon.data() : icon.data();
    if (ImGui::Button(icon_text, ImVec2(icon_button_size, icon_button_size))) {
      if (is_active) {
        panel_registry_->ClearActivePanel();
        ui_settings_vm_->SetActivePanelName(std::optional<std::string> {});
      } else {
        if (panel_registry_->SetActivePanelByName(entry.name)) {
          ui_settings_vm_->SetActivePanelName(entry.name);
        }
      }
    }

    if (is_active) {
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      const float underline_height = 3.0F * dpi_scale;
      const ImVec4 underline_color
        = ToSpectrumColor(imgui::spectrum::Static::kBlue500, 1.0F);
      ImDrawList* draw_list = ImGui::GetForegroundDrawList();
      draw_list->PushClipRect(min, max, true);
      draw_list->AddRectFilled(ImVec2(min.x + (6.0F * dpi_scale),
                                 max.y - underline_height - (2.0F * dpi_scale)),
        ImVec2(max.x - (6.0F * dpi_scale), max.y - (2.0F * dpi_scale)),
        ImGui::ColorConvertFloat4ToU32(underline_color));
      draw_list->PopClipRect();
    }

    if (ImGui::IsItemHovered()) {
      if (icon_font_24) {
        ImGui::PopFont();
      }
      ImFont* default_font = ImGui::GetIO().FontDefault;
      if (default_font) {
        ImGui::PushFont(default_font);
      }
      ImGui::SetTooltip("%s", entry.name.c_str());
      if (default_font) {
        ImGui::PopFont();
      }
      if (icon_font_24) {
        ImGui::PushFont(icon_font_24, kIconSize);
      }
    }

    // layout as a vertical column
  }

  ImGui::PopStyleVar();
  if (icon_font_24) {
    ImGui::PopFont();
  }
  ImGui::PopStyleVar(2);
  ImGui::End();
}

auto PanelSideBar::GetWidth() const noexcept -> float { return kSidebarWidth; }

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise)
