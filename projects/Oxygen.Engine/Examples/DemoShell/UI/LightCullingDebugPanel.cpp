//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>

#include "DemoShell/UI/LightCullingDebugPanel.h"
#include "DemoShell/UI/LightCullingVm.h"

namespace oxygen::examples::ui {

LightingPanel::LightingPanel(observer_ptr<LightCullingVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "LightingPanel requires LightCullingVm");
}

auto LightingPanel::DrawContents() -> void { DrawVisualizationModes(); }

auto LightingPanel::GetName() const noexcept -> std::string_view
{
  return "Lighting";
}

auto LightingPanel::GetPreferredWidth() const noexcept -> float
{
  return 360.0F;
}

auto LightingPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconLighting;
}

auto LightingPanel::OnRegistered() -> void
{
  // Settings are loaded via the ViewModel on construction.
}

auto LightingPanel::OnLoaded() -> void { }

auto LightingPanel::OnUnloaded() -> void
{
  // Persistence is handled by LightCullingSettingsService via the ViewModel.
}

void LightingPanel::DrawVisualizationModes()
{
  ImGui::SeparatorText("Visualization Modes");

  const auto current_mode = vm_->GetVisualizationMode();

  const bool is_lighting_mode = engine::IsLightCullingDebugMode(current_mode);

  const bool normal_selected
    = current_mode == ShaderDebugMode::kDisabled || !is_lighting_mode;

  if (ImGui::RadioButton("Normal", normal_selected)) {
    vm_->SetVisualizationMode(ShaderDebugMode::kDisabled);
  }

  for (const auto& info : vortex::EnumerateShaderDebugModes()) {
    if (info.family != vortex::ShaderDebugModeFamily::kLightCulling
      || !info.supported) {
      continue;
    }
    if (ImGui::RadioButton(info.display_name.data(), current_mode == info.mode)) {
      vm_->SetVisualizationMode(info.mode);
    }
  }
}

} // namespace oxygen::examples::ui
