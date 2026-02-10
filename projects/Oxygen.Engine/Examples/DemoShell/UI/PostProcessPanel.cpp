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

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

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
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Master switch for exposure control.");
  }

  if (!exposure_enabled) {
    ImGui::BeginDisabled();
  }

  auto mode_preview = "Manual (EV)";
  ExposureMode current_mode = vm_->GetExposureMode();
  if (current_mode == ExposureMode::kAuto) {
    mode_preview = "Automatic";
  } else if (current_mode == ExposureMode::kManualCamera) {
    mode_preview = "Manual (Camera)";
  }

  if (ImGui::BeginCombo("Mode##Exposure", mode_preview)) {
    if (ImGui::Selectable(
          "Manual (EV)", current_mode == ExposureMode::kManual)) {
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
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Select exposure control mode.");
  }

  if (current_mode == ExposureMode::kManual) {
    float ev = vm_->GetManualExposureEv();
    // Range roughly covering starlight to bright sunlight
    if (ImGui::DragFloat(
          "Manual Exposure (EV100)", &ev, 0.01F, 0.0F, 16.0F, "%.2f")) {
      ev = std::round(ev * 100.0F) / 100.0F;
      ev = std::max(ev, 0.0F);
      vm_->SetManualExposureEv(ev);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Scene luminance in Exposure Values (EV100). Higher values represent "
        "brighter light sources (e.g., 15 for sun), resulting in a darker "
        "image "
        "to maintain balance.");
    }
  }

  if (current_mode == ExposureMode::kManualCamera) {
    float aperture = vm_->GetManualCameraAperture();
    if (ImGui::DragFloat("Aperture (f/)", &aperture, 0.1F, 1.4F, 32.0F)) {
      vm_->SetManualCameraAperture(aperture);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Camera lens aperture (f-number).");
    }

    float shutter_rate = vm_->GetManualCameraShutterRate();
    if (ImGui::DragFloat("Shutter (1/s)", &shutter_rate, 1.0F, 1.0F, 8000.0F)) {
      vm_->SetManualCameraShutterRate(shutter_rate);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Camera shutter speed denominator (1/x seconds).");
    }

    float iso = vm_->GetManualCameraIso();
    if (ImGui::DragFloat("ISO", &iso, 10.0F, 100.0F, 6400.0F)) {
      vm_->SetManualCameraIso(iso);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Camera ISO sensitivity.");
    }

    const float computed_ev = vm_->GetManualCameraEv();
    ImGui::Text("Computed EV: %.2f", computed_ev);
  }

  if (current_mode == ExposureMode::kAuto) {
    float comp = vm_->GetExposureCompensation();
    if (ImGui::DragFloat("Compensation", &comp, 0.1F, -10.0F, 10.0F)) {
      vm_->SetExposureCompensation(comp);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Biases the target exposure (EV stops).");
    }
  }

  float exposure_key = vm_->GetExposureKey();
  if (ImGui::DragFloat("Exposure Key", &exposure_key, 0.1F, 0.1F, 25.0F)) {
    vm_->SetExposureKey(exposure_key);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Calibration constant (K). Scales global brightness. Default: 10.0.");
  }

  if (current_mode == ExposureMode::kAuto) {
    ImGui::Separator();
    ImGui::Text("Auto Exposure Settings");

    if (ImGui::Button("Reset Defaults")) {
      vm_->ResetAutoExposureDefaults();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Reset only auto-exposure settings to defaults.");
    }

    float speed_up = vm_->GetAutoExposureAdaptationSpeedUp();
    if (ImGui::DragFloat(
          "Adapt Speed Up", &speed_up, 0.1F, 0.1F, 20.0F, "%.1f EV/s")) {
      vm_->SetAutoExposureAdaptationSpeedUp(speed_up);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Speed of adaptation when transitioning from dark to bright.");
    }

    float speed_down = vm_->GetAutoExposureAdaptationSpeedDown();
    if (ImGui::DragFloat(
          "Adapt Speed Down", &speed_down, 0.1F, 0.1F, 20.0F, "%.1f EV/s")) {
      vm_->SetAutoExposureAdaptationSpeedDown(speed_down);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Speed of adaptation when transitioning from bright to dark.");
    }

    float low_pct = vm_->GetAutoExposureLowPercentile();
    if (ImGui::SliderFloat("Low Percentile", &low_pct, 0.0F, 1.0F)) {
      vm_->SetAutoExposureLowPercentile(low_pct);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Lower bound of histogram percentile for average luminance.");
    }

    float high_pct = vm_->GetAutoExposureHighPercentile();
    if (ImGui::SliderFloat("High Percentile", &high_pct, 0.0F, 1.0F)) {
      vm_->SetAutoExposureHighPercentile(high_pct);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Upper bound of histogram percentile for average luminance.");
    }

    float min_log = vm_->GetAutoExposureMinLogLuminance();
    if (ImGui::DragFloat("Min Log Lum", &min_log, 0.1F, -16.0F, 0.0F)) {
      vm_->SetAutoExposureMinLogLuminance(min_log);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Minimum luminance (log2) considered for auto exposure.");
    }

    float range_log = vm_->GetAutoExposureLogLuminanceRange();
    if (ImGui::DragFloat("Log Lum Range", &range_log, 0.1F, 1.0F, 32.0F)) {
      vm_->SetAutoExposureLogLuminanceRange(range_log);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Dynamic range (in stops) of the auto exposure histogram.");
    }

    float target_lum = vm_->GetAutoExposureTargetLuminance();
    if (ImGui::DragFloat("Target Lum", &target_lum, 0.01F, 0.01F, 1.0F)) {
      vm_->SetAutoExposureTargetLuminance(target_lum);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Target average luminance (middle gray) to aim for.");
    }

    engine::MeteringMode metering = vm_->GetAutoExposureMeteringMode();
    auto metering_str = "Average";
    if (metering == engine::MeteringMode::kCenterWeighted) {
      metering_str = "Center Weighted";
    } else if (metering == engine::MeteringMode::kSpot) {
      metering_str = "Spot";
    }

    if (ImGui::BeginCombo("Metering", metering_str)) {
      if (ImGui::Selectable(
            "Average", metering == engine::MeteringMode::kAverage)) {
        vm_->SetAutoExposureMeteringMode(engine::MeteringMode::kAverage);
      }
      if (ImGui::Selectable("Center Weighted",
            metering == engine::MeteringMode::kCenterWeighted)) {
        vm_->SetAutoExposureMeteringMode(engine::MeteringMode::kCenterWeighted);
      }
      if (ImGui::Selectable("Spot", metering == engine::MeteringMode::kSpot)) {
        vm_->SetAutoExposureMeteringMode(engine::MeteringMode::kSpot);
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Weighting method for calculating average luminance.");
    }
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

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
