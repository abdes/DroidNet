//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

#include "DemoShell/DemoKnobsViewModel.h"

namespace oxygen::engine {
struct LightCullingPassConfig;
} // namespace oxygen::engine

namespace oxygen::examples::render_scene::ui {

// Re-export ShaderDebugMode for convenience in UI code
using ShaderDebugMode = oxygen::engine::ShaderDebugMode;

//! Configuration shared by lighting/debug UI panels
struct LightCullingDebugConfig {
  //! Pointer to the shader pass config to control
  observer_ptr<oxygen::engine::ShaderPassConfig> shader_pass_config { nullptr };

  //! Pointer to the light culling pass config to control tile/cluster mode
  observer_ptr<oxygen::engine::LightCullingPassConfig>
    light_culling_pass_config { nullptr };

  //! Callback to notify when cluster mode changes (triggers PSO rebuild)
  std::function<void()> on_cluster_mode_changed {};

  //! Pointer to the demo knobs view model (rendering panel).
  observer_ptr<DemoKnobsViewModel> demo_knobs { nullptr };
};

//! Lighting panel with light culling and visualization controls
/*!
 Provides two collapsible sections: "Light Culling" and
 "Visualization Modes". Visualization modes toggle the shader debug
 mode automatically (Normal disables debug).
*/
class LightingPanel {
public:
  //! Initialize the panel with configuration
  void Initialize(const LightCullingDebugConfig& config);

  //! Update configuration (call when shader pass config changes)
  void UpdateConfig(const LightCullingDebugConfig& config);

  //! Draw the ImGui panel (call once per frame)
  void Draw();

  //! Draws the panel content without creating a window.
  void DrawContents();

private:
  void DrawVisualizationModes();
  void DrawLightCullingSettings();
  void DrawCullingModeControls();
  void DrawClusterConfigControls();
  void ApplyVisualizationMode(ShaderDebugMode mode);
  void ApplyCullingModeToPass();
  void ApplyClusterConfigToPass();

  LightCullingDebugConfig config_ {};
  bool use_clustered_culling_ { false };
  bool show_window_ { true };

  // Cluster config UI state (cached for editing)
  int ui_depth_slices_ { 24 };
  float ui_z_near_ { 0.1F };
  float ui_z_far_ { 1000.0F };
  bool ui_use_camera_z_ { true }; // Use camera near/far by default
};

} // namespace oxygen::examples::render_scene::ui
