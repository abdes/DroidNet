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

void LightCullingDebugPanel::Initialize(const LightCullingDebugConfig& config)
{
  config_ = config;
  current_mode_ = config.initial_mode;

  // Initialize clustered mode from current config
  if (config_.light_culling_pass_config) {
    const auto& cluster = config_.light_culling_pass_config->cluster;
    use_clustered_culling_ = cluster.depth_slices > 1;

    // Initialize UI state from config
    ui_depth_slices_ = static_cast<int>(cluster.depth_slices);
    ui_z_near_ = cluster.z_near;
    ui_z_far_ = cluster.z_far;
  }

  ApplySettingsToShaderPass();
}

void LightCullingDebugPanel::UpdateConfig(const LightCullingDebugConfig& config)
{
  config_ = config;
}

void LightCullingDebugPanel::Draw()
{
  if (!show_window_) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(1020, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 550), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(
        "Light Culling Debug", &show_window_, ImGuiWindowFlags_None)) {
    ImGui::End();
    return;
  }

  DrawContents();

  ImGui::End();
}

void LightCullingDebugPanel::DrawContents()
{
  DrawModeControls();
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  DrawCullingModeControls();
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  DrawClusterConfigControls();
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  DrawInfoSection();
}

void LightCullingDebugPanel::DrawModeControls()
{
  ImGui::SeparatorText("Debug Visualization");

  bool mode_changed = false;

  // Enable/Disable checkbox
  bool enabled = IsEnabled();
  if (ImGui::Checkbox("Enable Debug Overlay", &enabled)) {
    if (enabled && current_mode_ == ShaderDebugMode::kDisabled) {
      current_mode_ = ShaderDebugMode::kLightCullingHeatMap;
      mode_changed = true;
    } else if (!enabled) {
      current_mode_ = ShaderDebugMode::kDisabled;
      mode_changed = true;
    }
  }

  if (!enabled) {
    ImGui::BeginDisabled();
  }

  ImGui::Spacing();
  ImGui::Text("Visualization Mode:");

  // Light culling visualization modes
  if (ImGui::RadioButton(
        "Heat Map", current_mode_ == ShaderDebugMode::kLightCullingHeatMap)) {
    current_mode_ = ShaderDebugMode::kLightCullingHeatMap;
    mode_changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Light count heat map (smooth gradient):\n\n"
                      "  BLACK  = 0 lights\n"
                      "  GREEN  = 1-16 lights\n"
                      "  YELLOW = 17-32 lights\n"
                      "  RED    = 33-48 lights\n\n"
                      "Scale: 48 lights = maximum (full red)");
  }

  if (ImGui::RadioButton(
        "Slice Visualization", current_mode_ == ShaderDebugMode::kDepthSlice)) {
    current_mode_ = ShaderDebugMode::kDepthSlice;
    mode_changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Visualize depth slices with distinct colors.\n"
                      "Colors cycle: Red, Orange, Yellow, Green,\n"
                      "Pink, Dark Red, Dark Green, Light Yellow.\n\n"
                      "Only meaningful in clustered (3D) mode.\n"
                      "Gray = tile-based (no depth slices).");
  }

  if (ImGui::RadioButton(
        "Cluster Index", current_mode_ == ShaderDebugMode::kClusterIndex)) {
    current_mode_ = ShaderDebugMode::kClusterIndex;
    mode_changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Checkerboard pattern showing cluster boundaries.\n"
                      "Useful for verifying tile/cluster alignment.");
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Material / UV Debug");

  if (ImGui::RadioButton(
        "Base Color", current_mode_ == ShaderDebugMode::kBaseColor)) {
    current_mode_ = ShaderDebugMode::kBaseColor;
    mode_changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Visualize base color/albedo texture after UV transform.\n"
      "If this looks wrong, UVs or texture binding are wrong.");
  }

  if (ImGui::RadioButton("UV0", current_mode_ == ShaderDebugMode::kUv0)) {
    current_mode_ = ShaderDebugMode::kUv0;
    mode_changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Visualize UV0 as color (R=U, G=V).\n"
      "Solid gradients are correct; noisy patches imply bad UVs.");
  }

  if (ImGui::RadioButton(
        "Opacity", current_mode_ == ShaderDebugMode::kOpacity)) {
    current_mode_ = ShaderDebugMode::kOpacity;
    mode_changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Visualize base alpha/opacity.\n"
                      "White = fully opaque, black = transparent.");
  }

  if (!enabled) {
    ImGui::EndDisabled();
  }

  if (mode_changed) {
    ApplySettingsToShaderPass();
  }
}

void LightCullingDebugPanel::DrawCullingModeControls()
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

void LightCullingDebugPanel::DrawClusterConfigControls()
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

void LightCullingDebugPanel::DrawInfoSection()
{
  ImGui::SeparatorText("Information");

  // Show current culling mode
  ImGui::Text("Culling Mode: %s",
    use_clustered_culling_ ? "Clustered (3D)" : "Tile-Based (2D)");

  // Show actual config values (for debugging)
  if (config_.light_culling_pass_config) {
    const auto& cluster = config_.light_culling_pass_config->cluster;
    ImGui::TextColored(ImVec4(0.5F, 0.8F, 0.5F, 1.0F),
      "Config: slices=%u z=%.3f-%.1f", cluster.depth_slices, cluster.z_near,
      cluster.z_far);
  }

  if (IsEnabled()) {
    ImGui::Text("Debug Status: ACTIVE");

    const char* mode_name = "Unknown";
    switch (current_mode_) {
    case ShaderDebugMode::kLightCullingHeatMap:
      mode_name = "Heat Map";
      break;
    case ShaderDebugMode::kDepthSlice:
      mode_name = "Slice Visualization";
      break;
    case ShaderDebugMode::kClusterIndex:
      mode_name = "Cluster Index";
      break;
    default:
      break;
    }
    ImGui::Text("Visualization: %s", mode_name);
  } else {
    ImGui::TextColored(
      ImVec4(0.6F, 0.6F, 0.6F, 1.0F), "Debug Status: Disabled");
  }
}

void LightCullingDebugPanel::ApplySettingsToShaderPass()
{
  if (!config_.shader_pass_config) {
    return;
  }

  // Update the shader pass config with the debug mode
  config_.shader_pass_config->debug_mode = current_mode_;
}

void LightCullingDebugPanel::ApplyCullingModeToPass()
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

void LightCullingDebugPanel::ApplyClusterConfigToPass()
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
