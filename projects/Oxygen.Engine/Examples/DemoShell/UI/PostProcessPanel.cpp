//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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
  return imgui::icons::kIconRendering;
}

auto PostProcessPanel::DrawContents() -> void
{
  if (ImGui::CollapsingHeader("Compositing", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawCompositingSection();
  }

  if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawExposureSection();
  }

  if (ImGui::CollapsingHeader("Tonemapping", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawTonemappingSection();
  }
}

void PostProcessPanel::DrawCompositingSection()
{
  bool enabled = vm_->GetCompositingEnabled();
  if (ImGui::Checkbox("Enabled##Compositing", &enabled)) {
    vm_->SetCompositingEnabled(enabled);
  }

  if (!enabled) {
    ImGui::BeginDisabled();
  }

  float alpha = vm_->GetCompositingAlpha();
  if (ImGui::SliderFloat("Alpha", &alpha, 0.0F, 1.0F)) {
    vm_->SetCompositingAlpha(alpha);
  }

  if (!enabled) {
    ImGui::EndDisabled();
  }
}

void PostProcessPanel::DrawExposureSection()
{
  using engine::ExposureMode;
  using engine::ToneMapper;

  auto mode_preview = "Manual";
  ExposureMode current_mode = vm_->GetExposureMode();
  if (current_mode == ExposureMode::kAuto) {
    mode_preview = "Auto";
  }

  if (ImGui::BeginCombo("Mode##Exposure", mode_preview)) {
    if (ImGui::Selectable("Manual", current_mode == ExposureMode::kManual)) {
      vm_->SetExposureMode(ExposureMode::kManual);
    }
    if (ImGui::Selectable("Auto", current_mode == ExposureMode::kAuto)) {
      vm_->SetExposureMode(ExposureMode::kAuto);
    }
    ImGui::EndCombo();
  }

  if (current_mode == ExposureMode::kManual) {
    float ev100 = vm_->GetManualExposureEV100();
    // Range roughly covering starlight to bright sunlight
    if (ImGui::SliderFloat("EV100", &ev100, -6.0F, 16.0F)) {
      vm_->SetManualExposureEV100(ev100);
    }
  }

  float comp = vm_->GetExposureCompensation();
  if (ImGui::DragFloat("Compensation", &comp, 0.1F, -10.0F, 10.0F)) {
    vm_->SetExposureCompensation(comp);
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
