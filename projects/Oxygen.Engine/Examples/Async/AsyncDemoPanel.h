//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"

namespace oxygen::examples::async {

class AsyncDemoVm;

//! Panel for controlling Async Demo specific features.
/*!
 Replaces DroneControlPanel. Provides UI for scene info, spotlight settings,
 and frame profiling data, using AsyncDemoVm for state.
*/
class AsyncDemoPanel final : public oxygen::examples::DemoPanel {
public:
  explicit AsyncDemoPanel(observer_ptr<AsyncDemoVm> vm);
  ~AsyncDemoPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(AsyncDemoPanel)
  OXYGEN_MAKE_NON_MOVABLE(AsyncDemoPanel)

  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;

  auto OnLoaded() -> void override { }
  auto OnUnloaded() -> void override { }

private:
  void DrawSceneInfo();
  void DrawSpotlightControls();
  void DrawProfilingInfo();

  observer_ptr<AsyncDemoVm> vm_;
};

} // namespace oxygen::examples::async
