//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/UI/GridPanel.h"

namespace oxygen::examples::ui {

GridPanel::GridPanel(observer_ptr<GridVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "GridPanel requires GridVm");
}

auto GridPanel::DrawContents() -> void
{
  DrawGridSection();
  DrawFadeSection();
  DrawColorSection();
  DrawRenderSection();
}

auto GridPanel::GetName() const noexcept -> std::string_view
{
  return "Ground Grid";
}

auto GridPanel::GetPreferredWidth() const noexcept -> float { return 320.0F; }

auto GridPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconGrid2x2;
}

void GridPanel::DrawGridSection()
{
  if (!ImGui::CollapsingHeader("Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  bool enabled = vm_->GetEnabled();
  if (ImGui::Checkbox("Enabled", &enabled)) {
    vm_->SetEnabled(enabled);
  }

  float spacing = vm_->GetGridSpacing();
  if (ImGui::DragFloat("Spacing", &spacing, 0.1F, 0.01F, 100.0F, "%.2f")) {
    vm_->SetGridSpacing(spacing);
  }

  int major_every = vm_->GetMajorEvery();
  if (ImGui::DragInt("Major Every", &major_every, 1.0F, 1, 100)) {
    vm_->SetMajorEvery(major_every);
  }

  float line_thickness = vm_->GetLineThickness();
  if (ImGui::DragFloat(
        "Line Thickness", &line_thickness, 0.01F, 0.0F, 0.10F, "%.2f")) {
    vm_->SetLineThickness(line_thickness);
  }

  float major_thickness = vm_->GetMajorThickness();
  if (ImGui::DragFloat(
        "Major Thickness", &major_thickness, 0.01F, 0.0F, 0.10F, "%.2f")) {
    vm_->SetMajorThickness(major_thickness);
  }

  float axis_thickness = vm_->GetAxisThickness();
  if (ImGui::DragFloat(
        "Axis Thickness", &axis_thickness, 0.01F, 0.0F, 0.10F, "%.2f")) {
    vm_->SetAxisThickness(axis_thickness);
  }
}

void GridPanel::DrawFadeSection()
{
  if (!ImGui::CollapsingHeader("Fade", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  float fade_start = vm_->GetFadeStart();
  if (ImGui::DragFloat(
        "Fade Start", &fade_start, 1.0F, 0.0F, 10000.0F, "%.1f")) {
    vm_->SetFadeStart(fade_start);
  }

  float fade_power = vm_->GetFadePower();
  if (ImGui::DragFloat(
        "Fade Power", &fade_power, 0.05F, 0.0F, 8.0F, "%.2f")) {
    vm_->SetFadePower(fade_power);
  }
}

void GridPanel::DrawColorSection()
{
  if (!ImGui::CollapsingHeader("Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  const ImGuiColorEditFlags color_flags
    = ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;

  auto minor = vm_->GetMinorColor();
  float minor_rgba[4] { minor.r, minor.g, minor.b, minor.a };
  if (ImGui::ColorEdit4("Minor Color", minor_rgba, color_flags)) {
    vm_->SetMinorColor(
      graphics::Color { minor_rgba[0], minor_rgba[1], minor_rgba[2],
        minor_rgba[3] });
  }

  auto major = vm_->GetMajorColor();
  float major_rgba[4] { major.r, major.g, major.b, major.a };
  if (ImGui::ColorEdit4("Major Color", major_rgba, color_flags)) {
    vm_->SetMajorColor(
      graphics::Color { major_rgba[0], major_rgba[1], major_rgba[2],
        major_rgba[3] });
  }

  auto axis_x = vm_->GetAxisColorX();
  float axis_x_rgba[4] { axis_x.r, axis_x.g, axis_x.b, axis_x.a };
  if (ImGui::ColorEdit4("Axis X Color", axis_x_rgba, color_flags)) {
    vm_->SetAxisColorX(
      graphics::Color { axis_x_rgba[0], axis_x_rgba[1], axis_x_rgba[2],
        axis_x_rgba[3] });
  }

  auto axis_y = vm_->GetAxisColorY();
  float axis_y_rgba[4] { axis_y.r, axis_y.g, axis_y.b, axis_y.a };
  if (ImGui::ColorEdit4("Axis Y Color", axis_y_rgba, color_flags)) {
    vm_->SetAxisColorY(
      graphics::Color { axis_y_rgba[0], axis_y_rgba[1], axis_y_rgba[2],
        axis_y_rgba[3] });
  }

  auto origin = vm_->GetOriginColor();
  float origin_rgba[4] { origin.r, origin.g, origin.b, origin.a };
  if (ImGui::ColorEdit4("Origin Color", origin_rgba, color_flags)) {
    vm_->SetOriginColor(
      graphics::Color { origin_rgba[0], origin_rgba[1], origin_rgba[2],
        origin_rgba[3] });
  }
}

void GridPanel::DrawRenderSection()
{
  if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  float horizon_boost = vm_->GetHorizonBoost();
  if (ImGui::DragFloat(
        "Horizon Boost", &horizon_boost, 0.05F, 0.0F, 4.0F, "%.2f")) {
    vm_->SetHorizonBoost(horizon_boost);
  }
}

} // namespace oxygen::examples::ui
