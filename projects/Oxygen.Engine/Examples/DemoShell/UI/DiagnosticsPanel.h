//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/Runtime/RendererUiTypes.h"

#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/DiagnosticsVm.h"

namespace oxygen::examples::ui {

class DiagnosticsVm;

//! Diagnostics panel with Vortex runtime status and debug controls.
/*!
 This panel follows the MVVM pattern, receiving a DiagnosticsVm that owns
 the requested UI state and exposes the renderer effective state.
*/
class DiagnosticsPanel final : public DemoPanel {
public:
  //! Create the panel bound to a rendering view model.
  explicit DiagnosticsPanel(observer_ptr<DiagnosticsVm> vm);

  //! Draws the panel content without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnRegistered() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

  //! Get the current render mode from the view model.
  [[nodiscard]] auto GetRenderMode() const -> renderer::RenderMode;

private:
  void DrawRuntimeStatus();
  void DrawRendererCapabilities();
  void DrawShadowSettings();
  void DrawViewModeControls();
  void DrawWireframeColor();
  void DrawDebugModes();

  observer_ptr<DiagnosticsVm> vm_;
};

} // namespace oxygen::examples::ui
