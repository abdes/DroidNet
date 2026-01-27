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

namespace oxygen::engine {
struct LightCullingPassConfig;
} // namespace oxygen::engine

namespace oxygen::examples::render_scene::ui {

// Re-export ShaderDebugMode for convenience in UI code
using ShaderDebugMode = oxygen::engine::ShaderDebugMode;

//! Configuration for light culling debug panel
struct LightCullingDebugConfig {
  //! Pointer to the shader pass config to control
  observer_ptr<oxygen::engine::ShaderPassConfig> shader_pass_config { nullptr };

  //! Pointer to the light culling pass config to control tile/cluster mode
  observer_ptr<oxygen::engine::LightCullingPassConfig>
    light_culling_pass_config { nullptr };

  //! Callback to notify when cluster mode changes (triggers PSO rebuild)
  std::function<void()> on_cluster_mode_changed {};

  //! Initial debug mode
  ShaderDebugMode initial_mode { ShaderDebugMode::kDisabled };
};

//! Light culling debug visualization panel
/*!
 Displays an ImGui panel for controlling light culling debug visualization.
 Provides controls for enabling/disabling the debug overlay, selecting
 visualization modes, and adjusting overlay parameters.

 ### Key Features

 - **Enable/Disable:** Toggle debug visualization on/off
 - **Visualization Modes:**
   - Heat Map: Color gradient based on light count (blue = few, red = many)
   - Discrete Colors: Distinct colors for light count ranges
   - Depth Slice: Visualize depth slices (clustered mode)
   - Cluster Index: Checkerboard pattern showing cluster boundaries
   - Base Color: Visualize base color/albedo texture
   - UV0: Visualize UV0 coordinates as color
   - Opacity: Visualize base alpha/opacity

 ### Usage Example

 ```cpp
 LightCullingDebugPanel panel;
 LightCullingDebugConfig config;
 config.shader_pass_config = shader_pass_config_.get();
 config.initial_mode = ShaderDebugMode::kLightCullingHeatMap;
 panel.Initialize(config);

 // In frame loop:
 panel.Draw();
 ```

 ### Integration

 The panel directly modifies the `debug_mode` field in the provided
 `ShaderPassConfig`. The ShaderPass compiles the pixel shader with the
 appropriate DEBUG_MODE define based on this setting.
*/
class LightCullingDebugPanel {
public:
  //! Initialize the panel with configuration
  void Initialize(const LightCullingDebugConfig& config);

  //! Update configuration (call when shader pass config changes)
  void UpdateConfig(const LightCullingDebugConfig& config);

  //! Draw the ImGui panel (call once per frame)
  void Draw();

  //! Draws the panel content without creating a window.
  void DrawContents();

  //! Get current debug mode
  [[nodiscard]] auto GetDebugMode() const -> ShaderDebugMode
  {
    return current_mode_;
  }

  //! Check if debug visualization is enabled
  [[nodiscard]] auto IsEnabled() const -> bool
  {
    return current_mode_ != ShaderDebugMode::kDisabled;
  }

private:
  void DrawModeControls();
  void DrawCullingModeControls();
  void DrawClusterConfigControls();
  void DrawInfoSection();
  void ApplySettingsToShaderPass();
  void ApplyCullingModeToPass();
  void ApplyClusterConfigToPass();

  LightCullingDebugConfig config_ {};
  ShaderDebugMode current_mode_ { ShaderDebugMode::kDisabled };
  bool use_clustered_culling_ { false };
  bool show_window_ { true };

  // Cluster config UI state (cached for editing)
  int ui_depth_slices_ { 24 };
  float ui_z_near_ { 0.1F };
  float ui_z_far_ { 1000.0F };
  bool ui_use_camera_z_ { true }; // Use camera near/far by default
};

} // namespace oxygen::examples::render_scene::ui
