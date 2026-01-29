//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/UI/DemoPanel.h"

namespace oxygen::examples::async {

class MainModule;

//! Drone control panel for the Async example.
class DroneControlPanel final : public DemoPanel {
public:
  explicit DroneControlPanel(observer_ptr<MainModule> owner);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "Drone Control";
  }

  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override
  {
    return 520.0F;
  }

  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override
  {
    return oxygen::imgui::icons::kIconDemoPanel;
  }

  auto DrawContents() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  auto LoadSettings() -> void;
  auto SaveSettings() -> void;

  observer_ptr<MainModule> owner_ { nullptr };
  bool scene_open_ { true };
  bool spotlight_open_ { false };
  bool actions_open_ { true };
};

} // namespace oxygen::examples::async
