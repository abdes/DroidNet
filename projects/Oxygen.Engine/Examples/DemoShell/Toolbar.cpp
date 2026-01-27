//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>

#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>
#include <Oxygen/ImGui/Styles/Spectrum.h>

#include "DemoShell/Toolbar.h"

namespace oxygen::examples::demo_shell {

namespace {

  constexpr float kToolbarHeight = 44.0F;
  constexpr float kToolbarPadding = 8.0F;
  constexpr float kToolbarItemSpacing = 12.0F;

  struct SegmentedIconOption {
    const char* icon;
    const char* tooltip;
    bool active;
  };

  constexpr float kToolbarIconFontSize = 24.0F;
  constexpr float kSelectedUnderlineHeight = 4.0F;

  auto FindToolbarIconFont() -> ImFont*
  {
    auto& io = ImGui::GetIO();
    if (!io.Fonts || io.Fonts->Fonts.empty()) {
      return nullptr;
    }
    return io.Fonts->Fonts.back();
  }

  auto ToSpectrumColor(const unsigned int color, const float alpha) -> ImVec4
  {
    ImVec4 result = ImGui::ColorConvertU32ToFloat4(
      static_cast<ImU32>(color));
    result.w = alpha;
    return result;
  }

  template <std::size_t N>
  auto DrawSegmentedIconButtons(const char* id,
    const std::array<SegmentedIconOption, N>& options) -> int
  {
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0F, 7.0F));

    ImFont* icon_font = FindToolbarIconFont();
    const bool icon_font_pushed = icon_font != nullptr;
    if (icon_font_pushed) {
      ImGui::PushFont(icon_font);
    }

    const float button_size = ImGui::GetFrameHeight();
    int selected = -1;

    for (std::size_t i = 0; i < N; ++i) {
      const float rounding = (i == 0 || i + 1 == N)
        ? ImGui::GetStyle().FrameRounding
        : 0.0F;
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);

      const ImVec4 base_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const ImVec4 active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      const ImVec4 accent_color = ToSpectrumColor(
        oxygen::imgui::spectrum::Static::kBlue500, 0.35F);
      const ImVec4 button_color = options[i].active ? active_color : base_color;
      const ImVec4 button_hover = accent_color;

      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hover);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, active_color);

      if (ImGui::Button(options[i].icon, ImVec2(button_size, button_size))) {
        selected = static_cast<int>(i);
      }

      if (options[i].active) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float underline_bottom = max.y
          - std::max(1.0F, ImGui::GetStyle().FramePadding.y * 0.5F);
        const float underline_top = std::max(
          min.y, underline_bottom - kSelectedUnderlineHeight);
        const ImVec4 underline_color = ToSpectrumColor(
          oxygen::imgui::spectrum::Static::kBlue500, 1.0F);
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        draw_list->PushClipRect(min, max, true);
        draw_list->AddRectFilled(
          ImVec2(min.x + 1.0F, underline_top),
          ImVec2(max.x - 1.0F, std::min(underline_bottom, max.y)),
          ImGui::ColorConvertFloat4ToU32(underline_color));
        draw_list->PopClipRect();
      }

      if (ImGui::IsItemHovered()) {
        if (icon_font_pushed) {
          ImGui::PopFont();
        }
        ImFont* default_font = ImGui::GetIO().FontDefault;
        if (default_font) {
          ImGui::PushFont(default_font);
        }
        ImGui::SetTooltip("%s", options[i].tooltip);
        if (default_font) {
          ImGui::PopFont();
        }
        if (icon_font_pushed) {
          ImGui::PushFont(icon_font);
        }
      }

      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar();

      if (i + 1 < N) {
        ImGui::SameLine(0.0F, 0.0F);
      }
    }

    if (icon_font_pushed) {
      ImGui::PopFont();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return selected;
  }

} // namespace

auto Toolbar::Initialize(const ToolbarConfig& config) -> void
{
  config_ = config;
}

auto Toolbar::Draw() -> void
{
  if (!config_.knobs) {
    return;
  }

  const auto& io = ImGui::GetIO();
  height_ = kToolbarHeight;
  ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
  ImGui::SetNextWindowSize(
    ImVec2(io.DisplaySize.x, kToolbarHeight), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.5F);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
    ImVec2(kToolbarPadding, kToolbarPadding * 0.5F));
  ImGui::PushStyleVar(
    ImGuiStyleVar_ItemSpacing, ImVec2(kToolbarItemSpacing, 0.0F));

  constexpr auto kFlags = ImGuiWindowFlags_NoDecoration
    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
    | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    | ImGuiWindowFlags_NoBringToFrontOnFocus;

  if (ImGui::Begin("DemoToolbar", nullptr, kFlags)) {
    height_ = ImGui::GetWindowSize().y;
    DrawPanelMenu();
    ImGui::SameLine();
    DrawKnobs();
    DrawStats();
  }
  ImGui::End();

  ImGui::PopStyleVar(2);
}

auto Toolbar::GetHeight() const noexcept -> float { return height_; }

auto Toolbar::DrawPanelMenu() -> void
{
  if (!config_.panel_registry) {
    ImFont* icon_font = FindToolbarIconFont();
    if (icon_font) {
      ImGui::PushFont(icon_font);
    }
    ImGui::Button(ICON_FA_LIST_ALT);
    if (icon_font) {
      ImGui::PopFont();
    }
    return;
  }

  ImFont* icon_font = FindToolbarIconFont();
  if (icon_font) {
    ImGui::PushFont(icon_font);
  }
  const bool clicked = ImGui::Button(ICON_FA_LIST_ALT);
  if (icon_font) {
    ImGui::PopFont();
  }
  if (clicked) {
    ImGui::OpenPopup("DemoShellPanelMenu");
  }

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Menu");
  }

  if (!ImGui::BeginPopup("DemoShellPanelMenu")) {
    return;
  }

  const auto active_name = config_.panel_registry->GetActivePanelName();

  if (ImGui::Selectable("None", active_name.empty())) {
    config_.panel_registry->ClearActivePanel();
  }

  ImGui::Separator();

  for (const auto& entry : config_.panel_registry->GetPanels()) {
    const bool is_active = entry.name == active_name;
    if (ImGui::Selectable(entry.name.c_str(), is_active)) {
      (void)config_.panel_registry->SetActivePanelByName(entry.name);
    }
  }

  ImGui::EndPopup();
}

auto Toolbar::DrawKnobs() -> void
{
  if (!config_.knobs) {
    return;
  }

  auto& knobs = *config_.knobs;
  const std::array<SegmentedIconOption, 2> render_options { {
    { ICON_FA_SQUARE, "Render: Solid",
      knobs.render_mode == RenderMode::kSolid },
    { ICON_FA_DOT_CIRCLE, "Render: Wireframe",
      knobs.render_mode == RenderMode::kWireframe },
  } };
  const int render_selected
    = DrawSegmentedIconButtons("render_mode", render_options);
  if (render_selected == 0) {
    knobs.render_mode = RenderMode::kSolid;
  } else if (render_selected == 1) {
    knobs.render_mode = RenderMode::kWireframe;
  }

  ImGui::SameLine();
  ImGui::Dummy(ImVec2(6.0F, 0.0F));
  ImGui::SameLine();

  const std::array<SegmentedIconOption, 2> camera_options { {
    { ICON_FA_PAPER_PLANE, "Camera: Fly",
      knobs.camera_mode == CameraMode::kFly },
    { ICON_FA_COMPASS, "Camera: Orbit",
      knobs.camera_mode == CameraMode::kOrbit },
  } };
  const int camera_selected
    = DrawSegmentedIconButtons("camera_mode", camera_options);
  if (camera_selected == 0) {
    knobs.camera_mode = CameraMode::kFly;
  } else if (camera_selected == 1) {
    knobs.camera_mode = CameraMode::kOrbit;
  }

  ImGui::SameLine();
  ImGui::Checkbox("Axes", &knobs.show_axes_widget);

  ImGui::SameLine();
  ImGui::Checkbox("Stats", &knobs.show_frame_stats);
}

auto Toolbar::DrawStats() -> void
{
  if (!config_.knobs || !config_.knobs->show_frame_stats) {
    return;
  }

  const auto& io = ImGui::GetIO();
  const auto text = fmt::format("FPS {:.1f}", io.Framerate);

  const float available = ImGui::GetContentRegionAvail().x;
  const float text_width = ImGui::CalcTextSize(text.c_str()).x;
  ImGui::SameLine(
    std::max(0.0F, ImGui::GetCursorPosX() + available - text_width));
  ImGui::TextUnformatted(text.c_str());
}

} // namespace oxygen::examples::demo_shell
