//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>

#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

#include "DemoShell/Toolbar.h"

namespace oxygen::examples::demo_shell {

namespace {

  constexpr float kToolbarHeight = 36.0F;
  constexpr float kToolbarPadding = 8.0F;
  constexpr float kToolbarItemSpacing = 12.0F;

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
    ImGui::Button(ICON_FA_LIST_ALT);
    return;
  }

  if (ImGui::Button(ICON_FA_LIST_ALT)) {
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
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Render");
  ImGui::SameLine();

  const std::array<const char*, 2> render_labels { "Solid", "Wireframe" };
  int render_index = (knobs.render_mode == RenderMode::kWireframe) ? 1 : 0;
  if (ImGui::SetNextItemWidth(110.0F);
    ImGui::Combo("##render_mode", &render_index, render_labels.data(),
      static_cast<int>(render_labels.size()))) {
    knobs.render_mode
      = (render_index == 1) ? RenderMode::kWireframe : RenderMode::kSolid;
  }

  ImGui::SameLine();
  ImGui::TextUnformatted("Camera");
  ImGui::SameLine();

  const std::array<const char*, 2> camera_labels { "Fly", "Orbit" };
  int camera_index = (knobs.camera_mode == CameraMode::kOrbit) ? 1 : 0;
  if (ImGui::SetNextItemWidth(90.0F);
    ImGui::Combo("##camera_mode", &camera_index, camera_labels.data(),
      static_cast<int>(camera_labels.size()))) {
    knobs.camera_mode
      = (camera_index == 1) ? CameraMode::kOrbit : CameraMode::kFly;
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
