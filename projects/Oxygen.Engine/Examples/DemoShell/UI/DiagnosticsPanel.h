//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include "DemoShell/UI/DemoPanel.h"

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class DiagnosticsVm;

//! Sky-light validation and the debug drawing the toolbar does not cover.
/*!
 The view mode (lit, wireframe, every shader debug mode) is the host
 toolbar's `host.view_mode`, and GPU timing is its GPU work overlay. This
 panel shows the sky-light pipeline state with its GPU validation, and
 controls frame diagnostics, the draw-bounds debug pass and the color the
 wireframe view modes draw with. Every control runs a `demo.diag.*`
 command.
*/
class DiagnosticsPanel final : public DemoPanel {
public:
  DiagnosticsPanel(
    observer_ptr<DiagnosticsVm> vm, observer_ptr<PanelCommands> commands);

  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnRegistered() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  auto DrawFrameDiagnostics() -> void;
  auto DrawSkyLight() -> void;
  auto DrawDebugDrawing() -> void;

  observer_ptr<DiagnosticsVm> vm_;
  observer_ptr<PanelCommands> commands_;
};

} // namespace oxygen::examples::ui
