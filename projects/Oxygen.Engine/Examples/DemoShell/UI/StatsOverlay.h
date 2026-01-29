//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/UiSettingsVm.h"

namespace oxygen::examples::ui {

//! Transparent overlay for engine statistics.
/*!
 Draws a right-aligned stats overlay for FPS and frame timing. The overlay is
 non-interactive and designed to be shared across demos.
 */
class StatsOverlay {
public:
  explicit StatsOverlay(observer_ptr<UiSettingsVm> settings_vm);
  ~StatsOverlay() = default;

  OXYGEN_MAKE_NON_COPYABLE(StatsOverlay)
  OXYGEN_DEFAULT_MOVABLE(StatsOverlay)

  //! Draw the stats overlay.
  auto Draw() const -> void;

private:
  observer_ptr<UiSettingsVm> vm_ {};
};

} // namespace oxygen::examples::ui
