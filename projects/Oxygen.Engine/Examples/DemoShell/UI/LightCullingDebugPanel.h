//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

#include "DemoShell/UI/DemoPanel.h"

namespace oxygen::examples::ui {

class LightCullingVm;

// Re-export ShaderDebugMode for convenience in UI code
using ShaderDebugMode = oxygen::engine::ShaderDebugMode;

//! Lighting panel with light culling and visualization controls
/*!
 Provides two collapsible sections: "Light Culling" and
 "Visualization Modes". Visualization modes toggle the shader debug
 mode automatically (Normal disables debug).

 This panel follows the MVVM pattern, receiving a LightCullingVm that owns
 the state and handles persistence.
*/
class LightingPanel final : public DemoPanel {
public:
  //! Create the panel bound to a light culling view model.
  explicit LightingPanel(observer_ptr<LightCullingVm> vm);

  //! Draws the panel content without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnRegistered() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  void DrawVisualizationModes();
  void DrawLightCullingSettings();
  void DrawCullingModeControls();
  void DrawClusterConfigControls();

  observer_ptr<LightCullingVm> vm_ {};
};

} // namespace oxygen::examples::ui
