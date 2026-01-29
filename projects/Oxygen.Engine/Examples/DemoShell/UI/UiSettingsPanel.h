//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"

namespace oxygen::examples::ui {
class UiSettingsVm;
} // namespace oxygen::examples::ui

namespace oxygen::examples::ui {

//! Settings panel for UI visibility and stats toggles.
/*!
 Provides axis visibility control and a stats section for FPS and frame
 timing display.
*/
class UiSettingsPanel final : public DemoPanel {
public:
  //! Create the panel bound to a UI settings service.
  explicit UiSettingsPanel(observer_ptr<UiSettingsVm> settings_vm);

  //! Draw the panel contents without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnRegistered() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  void DrawStatsSection();

  observer_ptr<UiSettingsVm> vm_ {};
};

} // namespace oxygen::examples::ui
