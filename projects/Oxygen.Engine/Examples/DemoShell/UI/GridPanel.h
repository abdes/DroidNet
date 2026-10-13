//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/GridVm.h"

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

//! Panel for configuring the demo ground grid; every control runs a
//! `demo.grid.*` command.
class GridPanel final : public DemoPanel {
public:
  GridPanel(observer_ptr<GridVm> vm, observer_ptr<PanelCommands> commands);

  auto DrawContents() -> void override;
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;

  auto OnRegistered() -> void override { }
  auto OnLoaded() -> void override { }
  auto OnUnloaded() -> void override { }

private:
  void DrawGridSection();
  void DrawFadeSection();
  void DrawColorSection();
  void DrawRenderSection();

  observer_ptr<GridVm> vm_;
  observer_ptr<PanelCommands> commands_;
};

} // namespace oxygen::examples::ui
