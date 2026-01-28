//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>

#include "RenderScene/UI/LightCullingDebugPanel.h"

namespace oxygen::examples::render_scene::ui {

void LightingPanel::Initialize(const LightCullingDebugConfig& config)
{
  config_ = config;

  if (config_.light_culling_pass_config) {
    const auto& cluster = config_.light_culling_pass_config->cluster;
    use_clustered_culling_ = cluster.depth_slices > 1;
    ui_depth_slices_ = static_cast<int>(cluster.depth_slices);
    ui_z_near_ = cluster.z_near;
    ui_z_far_ = cluster.z_far;
  }
}

void LightingPanel::UpdateConfig(const LightCullingDebugConfig& config)
{
  config_ = config;
  if (config_.light_culling_pass_config) {
    const auto& cluster = config_.light_culling_pass_config->cluster;
    use_clustered_culling_ = cluster.depth_slices > 1;
    ui_depth_slices_ = static_cast<int>(cluster.depth_slices);
    ui_z_near_ = cluster.z_near;
    ui_z_far_ = cluster.z_far;
  }
}

void LightingPanel::Draw()
{
  if (!show_window_) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(1020, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Lighting", &show_window_, ImGuiWindowFlags_None)) {
    ImGui::End();
    return;
  }

  DrawContents();

  ImGui::End();
}

void LightingPanel::DrawContents()
{
  if (ImGui::CollapsingHeader(
        "Light Culling", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawLightCullingSettings();
  }

  if (ImGui::CollapsingHeader(
        "Visualization Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawVisualizationModes();
  }
}

void LightingPanel::DrawVisualizationModes()
{
  const auto current_mode = config_.shader_pass_config
    ? config_.shader_pass_config->debug_mode
    : ShaderDebugMode::kDisabled;

  const bool is_lighting_mode = current_mode == ShaderDebugMode::kDepthSlice
    || current_mode == ShaderDebugMode::kClusterIndex
    || current_mode == ShaderDebugMode::kLightCullingHeatMap;

  const bool normal_selected
    = (current_mode == ShaderDebugMode::kDisabled) || !is_lighting_mode;

  if (ImGui::RadioButton("Normal", normal_selected)) {
    ApplyVisualizationMode(ShaderDebugMode::kDisabled);
  }

  if (ImGui::RadioButton(
        "Heat Map", current_mode == ShaderDebugMode::kLightCullingHeatMap)) {
    ApplyVisualizationMode(ShaderDebugMode::kLightCullingHeatMap);
  }

  if (ImGui::RadioButton(
        "Slices", current_mode == ShaderDebugMode::kDepthSlice)) {
    ApplyVisualizationMode(ShaderDebugMode::kDepthSlice);
  }

  if (ImGui::RadioButton(
        "Clusters", current_mode == ShaderDebugMode::kClusterIndex)) {
    ApplyVisualizationMode(ShaderDebugMode::kClusterIndex);
  }
}

void LightingPanel::DrawLightCullingSettings()
{
  DrawCullingModeControls();
  ImGui::Spacing();
  DrawClusterConfigControls();
}

void LightingPanel::ApplyVisualizationMode(ShaderDebugMode mode)
{
  if (!config_.shader_pass_config) {
    return;
  }
  config_.shader_pass_config->debug_mode = mode;
}

void LightingPanel::DrawCullingModeControls()
{
  ImGui::SeparatorText("Culling Algorithm");

  bool mode_changed = false;

  // Radio buttons for tile vs clustered
  if (ImGui::RadioButton("Tile-Based (2D)", !use_clustered_culling_)) {
    if (use_clustered_culling_) {
      use_clustered_culling_ = false;
      mode_changed = true;
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Traditional Forward+ tiled culling.\n"
                      "Uses per-tile depth bounds from depth prepass.\n"
                      "Efficient for most scenes.");
  }

  if (ImGui::RadioButton("Clustered (3D)", use_clustered_culling_)) {
    if (!use_clustered_culling_) {
      use_clustered_culling_ = true;
      mode_changed = true;
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Full 3D clustered culling with depth slices.\n"
      "Uses logarithmic depth distribution.\n"
      "Better for depth-complex scenes with many overlapping lights.");
  }

  if (mode_changed) {
    ApplyCullingModeToPass();
  }
}

void LightingPanel::DrawClusterConfigControls()
{
  ImGui::SeparatorText("Cluster Configuration");

  if (!config_.light_culling_pass_config) {
    ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F), "No config available");
    return;
  }

  bool config_changed = false;

  // Tile size is fixed at 16x16 (compile-time constant in compute shader)
  ImGui::TextColored(
    ImVec4(0.7F, 0.7F, 0.7F, 1.0F), "Tile Size: 16x16 (fixed)");
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Tile size is a compile-time constant in the compute shader.\n"
      "16x16 is the optimal choice for most GPUs.");
  }

  // Only show depth slices control in clustered mode
  if (use_clustered_culling_) {
    if (ImGui::SliderInt("Depth Slices", &ui_depth_slices_, 2, 64)) {
      config_changed = true;
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
  if (ImGui::Checkbox("Use Camera Planes", &ui_use_camera_z_)) {
    config_changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Automatically use camera near/far planes.\n"
                      "Recommended for most scenes.");
  }

  if (!ui_use_camera_z_) {
    float z_near_log = std::log10(ui_z_near_);
    float z_far_log = std::log10(ui_z_far_);

    if (ImGui::SliderFloat("Z Near", &z_near_log, -2.0F, 2.0F, "10^%.2f")) {
      ui_z_near_ = std::pow(10.0F, z_near_log);
      // Ensure z_near < z_far
      if (ui_z_near_ >= ui_z_far_) {
        ui_z_near_ = ui_z_far_ * 0.1F;
      }
      config_changed = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Near plane for depth slicing (%.3f units).\n"
        "Should match or be slightly less than camera near plane.",
        ui_z_near_);
    }

    if (ImGui::SliderFloat("Z Far", &z_far_log, 1.0F, 4.0F, "10^%.2f")) {
      ui_z_far_ = std::pow(10.0F, z_far_log);
      // Ensure z_far > z_near
      if (ui_z_far_ <= ui_z_near_) {
        ui_z_far_ = ui_z_near_ * 10.0F;
      }
      config_changed = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Far plane for depth slicing (%.1f units).\n"
                        "Should match or exceed camera far plane.",
        ui_z_far_);
    }

    // Show actual values
    ImGui::TextColored(ImVec4(0.7F, 0.7F, 0.7F, 1.0F),
      "Range: %.3f - %.1f (ratio: %.0fx)", ui_z_near_, ui_z_far_,
      ui_z_far_ / ui_z_near_);
  } else {
    ImGui::TextColored(ImVec4(0.5F, 1.0F, 0.5F, 1.0F),
      "Using camera near/far planes automatically");
  }

  if (config_changed) {
    ApplyClusterConfigToPass();
  }
}

void LightingPanel::ApplyCullingModeToPass()
{
  if (!config_.light_culling_pass_config) {
    return;
  }

  // Update cluster config based on UI selection
  auto& cluster = config_.light_culling_pass_config->cluster;

  if (use_clustered_culling_) {
    // Switch to clustered with current UI settings
    cluster.depth_slices = static_cast<uint32_t>(ui_depth_slices_);
  } else {
    // Switch to tile-based (depth_slices = 1)
    cluster.depth_slices = 1;
  }

  // Tile size is fixed at 16 (compile-time constant in compute shader)
  cluster.tile_size_px = 16;
  cluster.z_near = ui_z_near_;
  cluster.z_far = ui_z_far_;

  // Notify that cluster mode changed (triggers PSO rebuild)
  if (config_.on_cluster_mode_changed) {
    config_.on_cluster_mode_changed();
  }
}

void LightingPanel::ApplyClusterConfigToPass()
{
  if (!config_.light_culling_pass_config) {
    LOG_F(WARNING, "ApplyClusterConfigToPass: No config!");
    return;
  }

  auto& cluster = config_.light_culling_pass_config->cluster;

  // Tile size is fixed at 16 (compile-time constant in compute shader)
  cluster.tile_size_px = 16;

  // Apply depth slices (only meaningful in clustered mode)
  if (use_clustered_culling_) {
    cluster.depth_slices = static_cast<uint32_t>(ui_depth_slices_);
  }

  // Apply Z range - 0 means "use camera near/far"
  if (ui_use_camera_z_) {
    cluster.z_near = 0.0F;
    cluster.z_far = 0.0F;
    LOG_F(INFO,
      "ApplyClusterConfigToPass: config={} depth_slices={} z_range=AUTO "
      "(camera)",
      fmt::ptr(config_.light_culling_pass_config.get()), cluster.depth_slices);
  } else {
    cluster.z_near = ui_z_near_;
    cluster.z_far = ui_z_far_;
    LOG_F(INFO,
      "ApplyClusterConfigToPass: config={} depth_slices={} z_near={:.4f} "
      "z_far={:.1f}",
      fmt::ptr(config_.light_culling_pass_config.get()), cluster.depth_slices,
      cluster.z_near, cluster.z_far);
  }

  // Notify that config changed (triggers buffer resize/rebuild)
  if (config_.on_cluster_mode_changed) {
    config_.on_cluster_mode_changed();
  }
}

} // namespace oxygen::examples::render_scene::ui
