//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/PanelRegistry.h"

namespace oxygen::examples::demo_shell {

struct PanelSideBarConfig {
  observer_ptr<PanelRegistry> panel_registry { nullptr };
};

class PanelSideBar {
public:
  PanelSideBar() = default;
  ~PanelSideBar() = default;

  PanelSideBar(const PanelSideBar&) = delete;
  auto operator=(const PanelSideBar&) -> PanelSideBar& = delete;
  PanelSideBar(PanelSideBar&&) = default;
  auto operator=(PanelSideBar&&) -> PanelSideBar& = default;

  auto Initialize(const PanelSideBarConfig& config) -> void;
  auto Draw() -> void;

  [[nodiscard]] auto GetWidth() const noexcept -> float
  {
    return kSidebarWidth;
  }

private:
  PanelSideBarConfig config_ {};
  static constexpr float kSidebarWidth = 120.0F;
  static constexpr float kIconSize = 24.0F;
};

} // namespace oxygen::examples::demo_shell
