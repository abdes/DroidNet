//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <string>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>

#include "DemoShell/UI/PostProcessPanel.h"

namespace oxygen::examples::ui {

PostProcessPanel::PostProcessPanel(observer_ptr<PostProcessVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "PostProcessPanel requires PostProcessVm");
}

auto PostProcessPanel::GetName() const noexcept -> std::string_view
{
  return "Post Process";
}

auto PostProcessPanel::GetPreferredWidth() const noexcept -> float
{
  return 320.0F;
}

auto PostProcessPanel::GetIcon() const noexcept -> std::string_view
{
  // Reusing Rendering icon as it fits best among available icons
  static const std::string kPostProcessIcon
    = std::string(imgui::icons::kIconHdrTonemap) + "##PostProcess";
  return kPostProcessIcon;
}

auto PostProcessPanel::DrawContents() -> void
{
  if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawExposureSection();
  }

  if (ImGui::CollapsingHeader("Tonemapping", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawTonemappingSection();
  }
}

void PostProcessPanel::DrawExposureSection()
{
  using engine::ExposureMode;

  bool exposure_enabled = vm_->GetExposureEnabled();
  if (ImGui::Checkbox("Enabled##Exposure", &exposure_enabled)) {
    vm_->SetExposureEnabled(exposure_enabled);
  }

  if (!exposure_enabled) {
    ImGui::BeginDisabled();
  }

  auto mode_preview = "Manual (EV100)";
  ExposureMode current_mode = vm_->GetExposureMode();
  if (current_mode == ExposureMode::kAuto) {
    mode_preview = "Automatic";
  } else if (current_mode == ExposureMode::kManualCamera) {
    mode_preview = "Manual (Camera)";
  }

  if (ImGui::BeginCombo("Mode##Exposure", mode_preview)) {
    if (ImGui::Selectable(
          "Manual (EV100)", current_mode == ExposureMode::kManual)) {
      vm_->SetExposureMode(ExposureMode::kManual);
    }
    if (ImGui::Selectable(
          "Manual (Camera)", current_mode == ExposureMode::kManualCamera)) {
      vm_->SetExposureMode(ExposureMode::kManualCamera);
    }
    if (ImGui::Selectable("Automatic", current_mode == ExposureMode::kAuto)) {
      vm_->SetExposureMode(ExposureMode::kAuto);
    }
    ImGui::EndCombo();
  }

  if (current_mode == ExposureMode::kManual) {
    float ev100 = vm_->GetManualExposureEV100();
    // Range roughly covering starlight to bright sunlight
    if (ImGui::DragFloat("EV100", &ev100, 0.01F, -6.0F, 16.0F, "%.2f")) {
      ev100 = std::round(ev100 * 100.0F) / 100.0F;
      vm_->SetManualExposureEV100(ev100);
    }
  }

  if (current_mode == ExposureMode::kManualCamera) {
    float aperture = vm_->GetManualCameraAperture();
    if (ImGui::DragFloat("Aperture (f/)", &aperture, 0.1F, 1.4F, 32.0F)) {
      vm_->SetManualCameraAperture(aperture);
    }

    float shutter_rate = vm_->GetManualCameraShutterRate();
    if (ImGui::DragFloat("Shutter (1/s)", &shutter_rate, 1.0F, 1.0F, 8000.0F)) {
      vm_->SetManualCameraShutterRate(shutter_rate);
    }

    float iso = vm_->GetManualCameraIso();
    if (ImGui::DragFloat("ISO", &iso, 10.0F, 100.0F, 6400.0F)) {
      vm_->SetManualCameraIso(iso);
    }

    const float computed_ev100 = vm_->GetManualCameraEV100();
    ImGui::Text("Computed EV100: %.2f", computed_ev100);
  }

  if (current_mode == ExposureMode::kAuto) {
    float comp = vm_->GetExposureCompensation();
    if (ImGui::DragFloat("Compensation", &comp, 0.1F, -10.0F, 10.0F)) {
      vm_->SetExposureCompensation(comp);
    }
  }

  if (current_mode == ExposureMode::kAuto) {
    ImGui::BeginDisabled();
    float speed_placeholder = 0.0F;
    ImGui::DragFloat(
      "Adapt Speed Up (EV/s)", &speed_placeholder, 0.1F, 0.1F, 20.0F);
    ImGui::DragFloat(
      "Adapt Speed Down (EV/s)", &speed_placeholder, 0.1F, 0.1F, 20.0F);
    ImGui::EndDisabled();
  }

  if (exposure_enabled) {
    if (current_mode == ExposureMode::kAuto) {
      ImGui::Text("Final Exposure (linear): Renderer (auto)");
    } else {
      ImGui::Text("Final Exposure (linear): Renderer");
    }
  } else {
    ImGui::Text("Final Exposure (linear): 1.0000 (disabled)");
  }

  if (!exposure_enabled) {
    ImGui::EndDisabled();
  }
}

void PostProcessPanel::DrawTonemappingSection()
{
  using engine::ToneMapper;

  bool enabled = vm_->GetTonemappingEnabled();
  if (ImGui::Checkbox("Enabled##Tonemapping", &enabled)) {
    vm_->SetTonemappingEnabled(enabled);
  }

  if (!enabled) {
    ImGui::BeginDisabled();
  }

  ToneMapper current_mode = vm_->GetToneMapper();
  auto mode_str = "Unknown";
  switch (current_mode) {
  case ToneMapper::kAcesFitted:
    mode_str = "ACES";
    break;
  case ToneMapper::kFilmic:
    mode_str = "Filmic";
    break;
  case ToneMapper::kReinhard:
    mode_str = "Reinhard";
    break;
  case ToneMapper::kNone:
    mode_str = "None";
    break;
  }

  if (ImGui::BeginCombo("Operator", mode_str)) {
    if (ImGui::Selectable("ACES", current_mode == ToneMapper::kAcesFitted)) {
      vm_->SetToneMapper(ToneMapper::kAcesFitted);
    }
    if (ImGui::Selectable("Filmic", current_mode == ToneMapper::kFilmic)) {
      vm_->SetToneMapper(ToneMapper::kFilmic);
    }
    if (ImGui::Selectable("Reinhard", current_mode == ToneMapper::kReinhard)) {
      vm_->SetToneMapper(ToneMapper::kReinhard);
    }
    ImGui::EndCombo();
  }

  if (!enabled) {
    ImGui::EndDisabled();
  }
}

} // namespace oxygen::examples::ui
