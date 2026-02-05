//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/UI/RenderingPanel.h"
#include "DemoShell/UI/RenderingVm.h"

namespace oxygen::examples::ui {

RenderingPanel::RenderingPanel(observer_ptr<RenderingVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "RenderingPanel requires RenderingVm");
}

auto RenderingPanel::DrawContents() -> void
{
  if (ImGui::CollapsingHeader("Render Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawViewModeControls();
    DrawWireframeColor();
  }

  if (ImGui::CollapsingHeader("Debug Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawDebugModes();
  }
}

auto RenderingPanel::GetName() const noexcept -> std::string_view
{
  return "Rendering";
}

auto RenderingPanel::GetPreferredWidth() const noexcept -> float
{
  return 320.0F;
}

auto RenderingPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconRendering;
}

auto RenderingPanel::OnRegistered() -> void { }

auto RenderingPanel::OnLoaded() -> void { }

auto RenderingPanel::OnUnloaded() -> void
{
  // Persistence is handled by RenderingSettingsService via the ViewModel.
}

auto RenderingPanel::GetRenderMode() const -> RenderMode
{
  return vm_->GetRenderMode();
}

void RenderingPanel::DrawViewModeControls()
{
  auto mode = vm_->GetRenderMode();

  if (ImGui::RadioButton("Solid", mode == RenderMode::kSolid)) {
    vm_->SetRenderMode(RenderMode::kSolid);
  }
  if (ImGui::RadioButton("Wireframe", mode == RenderMode::kWireframe)) {
    vm_->SetRenderMode(RenderMode::kWireframe);
  }
  if (ImGui::RadioButton(
        "Overlay Wireframe", mode == RenderMode::kOverlayWireframe)) {
    vm_->SetRenderMode(RenderMode::kOverlayWireframe);
  }
}

void RenderingPanel::DrawWireframeColor()
{
  const auto color = vm_->GetWireframeColor();
  float wire_color[3] = { color.r, color.g, color.b };
  if (ImGui::ColorEdit3("Wire Color", wire_color,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    vm_->SetWireframeColor(
      graphics::Color { wire_color[0], wire_color[1], wire_color[2], 1.0F });
  }
}

void RenderingPanel::DrawDebugModes()
{
  using engine::ShaderDebugMode;

  const auto current_mode = vm_->GetDebugMode();

  const bool is_ui_debug_mode = current_mode == ShaderDebugMode::kBaseColor
    || current_mode == ShaderDebugMode::kUv0
    || current_mode == ShaderDebugMode::kOpacity
    || current_mode == ShaderDebugMode::kIblSpecular
    || current_mode == ShaderDebugMode::kIblRawSky
    || current_mode == ShaderDebugMode::kIblIrradiance
    || current_mode == ShaderDebugMode::kWorldNormals
    || current_mode == ShaderDebugMode::kRoughness
    || current_mode == ShaderDebugMode::kMetalness;

  const bool normal_selected
    = current_mode == ShaderDebugMode::kDisabled || !is_ui_debug_mode;

  if (ImGui::RadioButton("Normal", normal_selected)) {
    vm_->SetDebugMode(ShaderDebugMode::kDisabled);
  }

  if (ImGui::RadioButton(
        "Base Color", current_mode == ShaderDebugMode::kBaseColor)) {
    vm_->SetDebugMode(ShaderDebugMode::kBaseColor);
  }

  if (ImGui::RadioButton("UV0", current_mode == ShaderDebugMode::kUv0)) {
    vm_->SetDebugMode(ShaderDebugMode::kUv0);
  }

  if (ImGui::RadioButton(
        "Opacity", current_mode == ShaderDebugMode::kOpacity)) {
    vm_->SetDebugMode(ShaderDebugMode::kOpacity);
  }

  if (ImGui::RadioButton(
        "World Normals", current_mode == ShaderDebugMode::kWorldNormals)) {
    vm_->SetDebugMode(ShaderDebugMode::kWorldNormals);
  }

  if (ImGui::RadioButton(
        "Roughness", current_mode == ShaderDebugMode::kRoughness)) {
    vm_->SetDebugMode(ShaderDebugMode::kRoughness);
  }

  if (ImGui::RadioButton(
        "Metalness", current_mode == ShaderDebugMode::kMetalness)) {
    vm_->SetDebugMode(ShaderDebugMode::kMetalness);
  }

  if (ImGui::RadioButton(
        "IBL Specular", current_mode == ShaderDebugMode::kIblSpecular)) {
    vm_->SetDebugMode(ShaderDebugMode::kIblSpecular);
  }

  if (ImGui::RadioButton(
        "IBL Irradiance", current_mode == ShaderDebugMode::kIblIrradiance)) {
    vm_->SetDebugMode(ShaderDebugMode::kIblIrradiance);
  }

  if (ImGui::RadioButton(
        "Sky Radiance", current_mode == ShaderDebugMode::kIblRawSky)) {
    vm_->SetDebugMode(ShaderDebugMode::kIblRawSky);
  }
}

} // namespace oxygen::examples::ui
