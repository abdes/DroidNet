//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
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
  if (ImGui::CollapsingHeader("View Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawViewModeControls();
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
  return oxygen::imgui::icons::kIconRendering;
}

auto RenderingPanel::OnRegistered() -> void { }

auto RenderingPanel::OnLoaded() -> void { }

auto RenderingPanel::OnUnloaded() -> void
{
  // Persistence is handled by RenderingSettingsService via the ViewModel.
}

auto RenderingPanel::GetViewMode() const -> RenderingViewMode
{
  return vm_->GetViewMode();
}

void RenderingPanel::DrawViewModeControls()
{
  auto mode = vm_->GetViewMode();

  if (ImGui::RadioButton("Solid", mode == RenderingViewMode::kSolid)) {
    vm_->SetViewMode(RenderingViewMode::kSolid);
  }
  if (ImGui::RadioButton("Wireframe", mode == RenderingViewMode::kWireframe)) {
    vm_->SetViewMode(RenderingViewMode::kWireframe);
  }
}

void RenderingPanel::DrawDebugModes()
{
  using engine::ShaderDebugMode;

  const auto current_mode = vm_->GetDebugMode();

  const bool is_rendering_mode = current_mode == ShaderDebugMode::kBaseColor
    || current_mode == ShaderDebugMode::kUv0
    || current_mode == ShaderDebugMode::kOpacity;

  const bool normal_selected
    = (current_mode == ShaderDebugMode::kDisabled) || !is_rendering_mode;

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

  if (ImGui::RadioButton("Opacity", current_mode == ShaderDebugMode::kOpacity)) {
    vm_->SetDebugMode(ShaderDebugMode::kOpacity);
  }
}

} // namespace oxygen::examples::ui
