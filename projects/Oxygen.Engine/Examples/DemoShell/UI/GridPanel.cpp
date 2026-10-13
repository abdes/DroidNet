//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/GridPanel.h"
#include "DemoShell/UI/GridVm.h"
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

// NOLINTBEGIN(bugprone-signed-bitwise) - ImGui

namespace oxygen::examples::ui {

namespace {

  constexpr std::string_view kSmoothMotion = "demo.grid.smooth_motion";
  constexpr std::string_view kSmoothTime = "demo.grid.smooth_time";
  constexpr std::string_view kSpacing = "demo.grid.spacing";
  constexpr std::string_view kMajorEvery = "demo.grid.major_every";
  constexpr std::string_view kLineThickness = "demo.grid.line_thickness";
  constexpr std::string_view kMajorThickness = "demo.grid.major_thickness";
  constexpr std::string_view kAxisThickness = "demo.grid.axis_thickness";
  constexpr std::string_view kFadeStart = "demo.grid.fade_start";
  constexpr std::string_view kFadePower = "demo.grid.fade_power";
  constexpr std::string_view kMinorColor = "demo.grid.minor_color";
  constexpr std::string_view kMajorColor = "demo.grid.major_color";
  constexpr std::string_view kAxisColorX = "demo.grid.axis_x_color";
  constexpr std::string_view kAxisColorY = "demo.grid.axis_y_color";
  constexpr std::string_view kOriginColor = "demo.grid.origin_color";
  constexpr std::string_view kHorizonBoost = "demo.grid.horizon_boost";

  auto RegisterCommands(PanelCommands& commands, observer_ptr<GridVm> vm)
    -> void
  {
    const auto name
      = [](const std::string_view n) -> std::string { return std::string(n); };
    commands.AddBool(name(kSmoothMotion), "Grid Smooth Motion",
      "Follow the camera smoothly instead of snapping",
      [vm](const bool v) -> void { vm->SetSmoothMotion(v); });
    commands.AddFloat(
      name(kSmoothTime), "Grid Smooth Time",
      "Seconds the grid takes to follow the camera",
      [vm](const float v) -> void { vm->SetSmoothTime(v); }, 0.001F, 10.0F);
    commands.AddFloat(
      name(kSpacing), "Grid Spacing", "Distance between minor lines, in meters",
      [vm](const float v) -> void { vm->SetGridSpacing(v); }, 0.01F, 100.0F);
    commands.AddInt(
      name(kMajorEvery), "Grid Major Every", "Minor cells between major lines",
      [vm](const int v) -> void { vm->SetMajorEvery(v); }, 1, 100);
    commands.AddFloat(
      name(kLineThickness), "Grid Line Thickness", "Minor line thickness",
      [vm](const float v) -> void { vm->SetLineThickness(v); }, 0.0F, 0.1F);
    commands.AddFloat(
      name(kMajorThickness), "Grid Major Thickness", "Major line thickness",
      [vm](const float v) -> void { vm->SetMajorThickness(v); }, 0.0F, 0.1F);
    commands.AddFloat(
      name(kAxisThickness), "Grid Axis Thickness", "Axis line thickness",
      [vm](const float v) -> void { vm->SetAxisThickness(v); }, 0.0F, 0.1F);
    commands.AddFloat(
      name(kFadeStart), "Grid Fade Start",
      "Distance where the grid starts to fade, in meters",
      [vm](const float v) -> void { vm->SetFadeStart(v); }, 0.0F, 10000.0F);
    commands.AddFloat(
      name(kFadePower), "Grid Fade Power", "Steepness of the fade",
      [vm](const float v) -> void { vm->SetFadePower(v); }, 0.0F, 8.0F);
    commands.AddColor(name(kMinorColor), "Grid Minor Color",
      "Color of the minor lines",
      [vm](const graphics::Color& c) -> void { vm->SetMinorColor(c); });
    commands.AddColor(name(kMajorColor), "Grid Major Color",
      "Color of the major lines",
      [vm](const graphics::Color& c) -> void { vm->SetMajorColor(c); });
    commands.AddColor(name(kAxisColorX), "Grid X Axis Color",
      "Color of the X axis line",
      [vm](const graphics::Color& c) -> void { vm->SetAxisColorX(c); });
    commands.AddColor(name(kAxisColorY), "Grid Y Axis Color",
      "Color of the Y axis line",
      [vm](const graphics::Color& c) -> void { vm->SetAxisColorY(c); });
    commands.AddColor(name(kOriginColor), "Grid Origin Color",
      "Color of the origin marker",
      [vm](const graphics::Color& c) -> void { vm->SetOriginColor(c); });
    commands.AddFloat(
      name(kHorizonBoost), "Grid Horizon Boost", "Line boost near the horizon",
      [vm](const float v) -> void { vm->SetHorizonBoost(v); }, 0.0F, 4.0F);
  }

  //! An HDR color editor; the edited color when the user changed it.
  auto EditColor(const char* label, const graphics::Color& color)
    -> std::optional<graphics::Color>
  {
    auto rgba = std::array { color.r, color.g, color.b, color.a };
    if (!ImGui::ColorEdit4(label, rgba.data(),
          ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) {
      return std::nullopt;
    }
    return graphics::Color { rgba[0], rgba[1], rgba[2], rgba[3] };
  }

} // namespace

GridPanel::GridPanel(
  observer_ptr<GridVm> vm, observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
{
  DCHECK_NOTNULL_F(vm_, "GridPanel requires GridVm");
  DCHECK_NOTNULL_F(commands_, "GridPanel requires PanelCommands");
  RegisterCommands(*commands_, vm_);
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

  // Showing or hiding the grid is the toolbar's Show > Grid.
  bool smooth_motion = vm_->GetSmoothMotion();
  if (ImGui::Checkbox("Smooth Motion", &smooth_motion)) {
    commands_->RunBool(kSmoothMotion, smooth_motion);
  }

  if (smooth_motion) {
    float smooth_time = vm_->GetSmoothTime();
    if (ImGui::DragFloat(
          "Smooth Time", &smooth_time, 0.05F, 0.001F, 10.0F, "%.3f")) {
      commands_->RunFloat(kSmoothTime, smooth_time);
    }
  }

  float spacing = vm_->GetGridSpacing();
  if (ImGui::DragFloat("Spacing", &spacing, 0.1F, 0.01F, 100.0F, "%.2f")) {
    commands_->RunFloat(kSpacing, spacing);
  }

  int major_every = vm_->GetMajorEvery();
  if (ImGui::DragInt("Major Every", &major_every, 1.0F, 1, 100)) {
    commands_->RunInt(kMajorEvery, major_every);
  }

  float line_thickness = vm_->GetLineThickness();
  if (ImGui::DragFloat(
        "Line Thickness", &line_thickness, 0.01F, 0.0F, 0.10F, "%.2f")) {
    commands_->RunFloat(kLineThickness, line_thickness);
  }

  float major_thickness = vm_->GetMajorThickness();
  if (ImGui::DragFloat(
        "Major Thickness", &major_thickness, 0.01F, 0.0F, 0.10F, "%.2f")) {
    commands_->RunFloat(kMajorThickness, major_thickness);
  }

  float axis_thickness = vm_->GetAxisThickness();
  if (ImGui::DragFloat(
        "Axis Thickness", &axis_thickness, 0.01F, 0.0F, 0.10F, "%.2f")) {
    commands_->RunFloat(kAxisThickness, axis_thickness);
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
    commands_->RunFloat(kFadeStart, fade_start);
  }

  float fade_power = vm_->GetFadePower();
  if (ImGui::DragFloat("Fade Power", &fade_power, 0.05F, 0.0F, 8.0F, "%.2f")) {
    commands_->RunFloat(kFadePower, fade_power);
  }
}

void GridPanel::DrawColorSection()
{
  if (!ImGui::CollapsingHeader("Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  if (const auto color = EditColor("Minor Color", vm_->GetMinorColor())) {
    commands_->RunColor(kMinorColor, *color);
  }
  if (const auto color = EditColor("Major Color", vm_->GetMajorColor())) {
    commands_->RunColor(kMajorColor, *color);
  }
  if (const auto color = EditColor("Axis X Color", vm_->GetAxisColorX())) {
    commands_->RunColor(kAxisColorX, *color);
  }
  if (const auto color = EditColor("Axis Y Color", vm_->GetAxisColorY())) {
    commands_->RunColor(kAxisColorY, *color);
  }
  if (const auto color = EditColor("Origin Color", vm_->GetOriginColor())) {
    commands_->RunColor(kOriginColor, *color);
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
    commands_->RunFloat(kHorizonBoost, horizon_boost);
  }
}

} // namespace oxygen::examples::ui

// NOLINTEND(bugprone-signed-bitwise)
