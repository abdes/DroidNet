//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/UI/RenderingPanel.h"

namespace oxygen::examples::ui {

void RenderingPanel::Initialize(const LightCullingDebugConfig& config)
{
  config_ = config;
}

void RenderingPanel::UpdateConfig(const LightCullingDebugConfig& config)
{
  config_ = config;
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

auto RenderingPanel::OnLoaded() -> void { }

auto RenderingPanel::OnUnloaded() -> void { }

void RenderingPanel::DrawViewModeControls()
{
  if (ImGui::RadioButton("Solid", view_mode_ == RenderingViewMode::kSolid)) {
    view_mode_ = RenderingViewMode::kSolid;
  }
  if (ImGui::RadioButton(
        "Wireframe", view_mode_ == RenderingViewMode::kWireframe)) {
    view_mode_ = RenderingViewMode::kWireframe;
  }
}

void RenderingPanel::DrawDebugModes()
{
  const auto current_mode = config_.shader_pass_config
    ? config_.shader_pass_config->debug_mode
    : ShaderDebugMode::kDisabled;

  const bool is_rendering_mode = current_mode == ShaderDebugMode::kBaseColor
    || current_mode == ShaderDebugMode::kUv0
    || current_mode == ShaderDebugMode::kOpacity;

  const bool normal_selected
    = (current_mode == ShaderDebugMode::kDisabled) || !is_rendering_mode;

  if (ImGui::RadioButton("Normal", normal_selected)) {
    ApplyDebugMode(ShaderDebugMode::kDisabled);
  }

  if (ImGui::RadioButton(
        "Base Color", current_mode == ShaderDebugMode::kBaseColor)) {
    ApplyDebugMode(ShaderDebugMode::kBaseColor);
  }

  if (ImGui::RadioButton("UV0", current_mode == ShaderDebugMode::kUv0)) {
    ApplyDebugMode(ShaderDebugMode::kUv0);
  }

  if (ImGui::RadioButton(
        "Opacity", current_mode == ShaderDebugMode::kOpacity)) {
    ApplyDebugMode(ShaderDebugMode::kOpacity);
  }
}

void RenderingPanel::ApplyDebugMode(ShaderDebugMode mode)
{
  if (!config_.shader_pass_config) {
    return;
  }
  config_.shader_pass_config->debug_mode = mode;
}

} // namespace oxygen::examples::ui
