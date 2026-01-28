//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "DemoShell/UI/LightCullingDebugPanel.h"

namespace oxygen::examples::ui {

//! View mode selection for rendering panel.
enum class RenderingViewMode { kSolid, kWireframe };

//! Rendering panel with view and debug mode controls
/*!
 Provides two collapsible sections: "View Mode" and "Debug Modes".
 Debug modes toggle the shader debug mode automatically (Normal disables
 debug).
*/
class RenderingPanel {
public:
  //! Initialize the panel with configuration
  void Initialize(const LightCullingDebugConfig& config);

  //! Update configuration (call when shader pass config changes)
  void UpdateConfig(const LightCullingDebugConfig& config);

  //! Draw the ImGui panel (call once per frame)
  void Draw();

  //! Draws the panel content without creating a window.
  void DrawContents();

  //! Set the current view mode.
  void SetViewMode(RenderingViewMode mode) { view_mode_ = mode; }

  //! Get the current view mode.
  [[nodiscard]] auto GetViewMode() const -> RenderingViewMode
  {
    return view_mode_;
  }

private:
  void DrawViewModeControls();
  void DrawDebugModes();
  void ApplyDebugMode(ShaderDebugMode mode);

  LightCullingDebugConfig config_ {};
  bool show_window_ { true };
  RenderingViewMode view_mode_ { RenderingViewMode::kSolid };
};

} // namespace oxygen::examples::ui
