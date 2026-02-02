//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/RenderingVm.h"

namespace oxygen::examples::ui {

class RenderingVm;

//! Rendering panel with view and debug mode controls
/*!
 Provides two collapsible sections: "View Mode" and "Debug Modes".
 Debug modes toggle the shader debug mode automatically (Normal disables
 debug).

 This panel follows the MVVM pattern, receiving a RenderingVm that owns
 the state and handles persistence.
*/
class RenderingPanel final : public DemoPanel {
public:
  //! Create the panel bound to a rendering view model.
  explicit RenderingPanel(observer_ptr<RenderingVm> vm);

  //! Draws the panel content without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnRegistered() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

  //! Get the current render mode from the view model.
  [[nodiscard]] auto GetRenderMode() const -> RenderMode;

private:
  void DrawViewModeControls();
  void DrawWireframeColor();
  void DrawDebugModes();

  observer_ptr<RenderingVm> vm_ {};
};

} // namespace oxygen::examples::ui
