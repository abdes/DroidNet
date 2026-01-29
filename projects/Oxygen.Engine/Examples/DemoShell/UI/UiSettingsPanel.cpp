//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/UI/UiSettingsPanel.h"
#include "DemoShell/UI/UiSettingsVm.h"

namespace oxygen::examples::ui {

UiSettingsPanel::UiSettingsPanel(observer_ptr<UiSettingsVm> settings_vm)
  : vm_(settings_vm)
{
  DCHECK_NOTNULL_F(settings_vm, "UiSettingsPanel requires UiSettingsVm");
}

auto UiSettingsPanel::DrawContents() -> void
{
  bool visible = vm_->GetAxesVisible();
  if (ImGui::Checkbox("Axis visibility", &visible)) {
    vm_->SetAxesVisible(visible);
  }
  ImGui::Spacing();

  if (ImGui::CollapsingHeader("Show Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawStatsSection();
  }
}

auto UiSettingsPanel::GetName() const noexcept -> std::string_view
{
  return "Settings";
}

auto UiSettingsPanel::GetPreferredWidth() const noexcept -> float
{
  return 320.0F;
}

auto UiSettingsPanel::GetIcon() const noexcept -> std::string_view
{
  return oxygen::imgui::icons::kIconSettings;
}

auto UiSettingsPanel::OnRegistered() -> void { }

auto UiSettingsPanel::OnLoaded() -> void { }

auto UiSettingsPanel::OnUnloaded() -> void
{
  // Settings persistence is owned by UiSettingsService.
}

void UiSettingsPanel::DrawStatsSection()
{
  auto config = vm_->GetStatsConfig();
  const bool hide_all = !config.show_fps && !config.show_frame_timing_detail;
  bool hide_all_toggle = hide_all;
  if (ImGui::Checkbox("Hide all", &hide_all_toggle) && hide_all_toggle) {
    if (config.show_fps) {
      vm_->SetStatsShowFps(false);
    }
    if (config.show_frame_timing_detail) {
      vm_->SetStatsShowFrameTimingDetail(false);
    }
    return;
  }

  if (ImGui::Checkbox("FPS", &config.show_fps)) {
    vm_->SetStatsShowFps(config.show_fps);
  }
  if (ImGui::Checkbox(
        "Frame timings detail", &config.show_frame_timing_detail)) {
    vm_->SetStatsShowFrameTimingDetail(config.show_frame_timing_detail);
  }
}

} // namespace oxygen::examples::ui
