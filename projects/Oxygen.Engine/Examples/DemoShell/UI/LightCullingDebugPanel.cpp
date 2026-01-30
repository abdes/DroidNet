//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/UI/LightCullingDebugPanel.h"
#include "DemoShell/UI/LightCullingVm.h"

namespace oxygen::examples::ui {

LightingPanel::LightingPanel(observer_ptr<LightCullingVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "LightingPanel requires LightCullingVm");
}

auto LightingPanel::DrawContents() -> void
{
  DrawVisualizationModes();
  DrawLightCullingSettings();
}

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
  return oxygen::imgui::icons::kIconLighting;
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

  const bool is_lighting_mode = current_mode == ShaderDebugMode::kDepthSlice
    || current_mode == ShaderDebugMode::kClusterIndex
    || current_mode == ShaderDebugMode::kLightCullingHeatMap;

  const bool normal_selected
    = (current_mode == ShaderDebugMode::kDisabled) || !is_lighting_mode;

  if (ImGui::RadioButton("Normal", normal_selected)) {
    vm_->SetVisualizationMode(ShaderDebugMode::kDisabled);
  }

  if (ImGui::RadioButton(
        "Heat Map", current_mode == ShaderDebugMode::kLightCullingHeatMap)) {
    vm_->SetVisualizationMode(ShaderDebugMode::kLightCullingHeatMap);
  }

  if (ImGui::RadioButton(
        "Slices", current_mode == ShaderDebugMode::kDepthSlice)) {
    vm_->SetVisualizationMode(ShaderDebugMode::kDepthSlice);
  }

  if (ImGui::RadioButton(
        "Clusters", current_mode == ShaderDebugMode::kClusterIndex)) {
    vm_->SetVisualizationMode(ShaderDebugMode::kClusterIndex);
  }
}

void LightingPanel::DrawLightCullingSettings()
{
  DrawCullingModeControls();
  ImGui::Spacing();
  DrawClusterConfigControls();
}

void LightingPanel::DrawCullingModeControls()
{
  ImGui::SeparatorText("Culling Algorithm");

  const bool use_clustered = vm_->IsClusteredCulling();

  // Radio buttons for tile vs clustered
  if (ImGui::RadioButton("Tile-Based (2D)", !use_clustered)) {
    if (use_clustered) {
      vm_->SetClusteredCulling(false);
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Traditional Forward+ tiled culling.\n"
                      "Uses per-tile depth bounds from depth prepass.\n"
                      "Efficient for most scenes.");
  }

  if (ImGui::RadioButton("Clustered (3D)", use_clustered)) {
    if (!use_clustered) {
      vm_->SetClusteredCulling(true);
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Full 3D clustered culling with depth slices.\n"
      "Uses logarithmic depth distribution.\n"
      "Better for depth-complex scenes with many overlapping lights.");
  }
}

void LightingPanel::DrawClusterConfigControls()
{
  ImGui::SeparatorText("Cluster Configuration");

  // Tile size is fixed at 16x16 (compile-time constant in compute shader)
  ImGui::TextColored(
    ImVec4(0.7F, 0.7F, 0.7F, 1.0F), "Tile Size: 16x16 (fixed)");
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Tile size is a compile-time constant in the compute shader.\n"
      "16x16 is the optimal choice for most GPUs.");
  }

  const bool use_clustered = vm_->IsClusteredCulling();

  // Only show depth slices control in clustered mode
  if (use_clustered) {
    int depth_slices = vm_->GetDepthSlices();
    if (ImGui::SliderInt("Depth Slices", &depth_slices, 2, 64)) {
      vm_->SetDepthSlices(depth_slices);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Number of depth slices for 3D clustering.\n"
                        "More slices = finer depth granularity.\n"
                        "16-32 is typical, 24 is default.");
    }
  }

  // Z range controls
  ImGui::Text("Depth Range:");

  // Checkbox for automatic camera-based depth range
  bool use_camera_z = vm_->GetUseCameraZ();
  if (ImGui::Checkbox("Use Camera Planes", &use_camera_z)) {
    vm_->SetUseCameraZ(use_camera_z);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Automatically use camera near/far planes.\n"
                      "Recommended for most scenes.");
  }

  if (!use_camera_z) {
    float z_near = vm_->GetZNear();
    float z_far = vm_->GetZFar();
    float z_near_log = std::log10(z_near);
    float z_far_log = std::log10(z_far);

    if (ImGui::SliderFloat("Z Near", &z_near_log, -2.0F, 2.0F, "10^%.2f")) {
      z_near = std::pow(10.0F, z_near_log);
      // Ensure z_near < z_far
      if (z_near >= z_far) {
        z_near = z_far * 0.1F;
      }
      vm_->SetZNear(z_near);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Near plane for depth slicing (%.3f units).\n"
        "Should match or be slightly less than camera near plane.",
        z_near);
    }

    if (ImGui::SliderFloat("Z Far", &z_far_log, 1.0F, 4.0F, "10^%.2f")) {
      z_far = std::pow(10.0F, z_far_log);
      // Ensure z_far > z_near
      if (z_far <= z_near) {
        z_far = z_near * 10.0F;
      }
      vm_->SetZFar(z_far);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Far plane for depth slicing (%.1f units).\n"
                        "Should match or exceed camera far plane.",
        z_far);
    }

    // Show actual values
    ImGui::TextColored(ImVec4(0.7F, 0.7F, 0.7F, 1.0F),
      "Range: %.3f - %.1f (ratio: %.0fx)", z_near, z_far, z_far / z_near);
  }
}

} // namespace oxygen::examples::ui
