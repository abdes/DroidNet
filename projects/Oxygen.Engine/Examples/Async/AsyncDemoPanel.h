//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include "DemoShell/UI/DemoPanel.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::async {

class AsyncDemoVm;

//! The tour, the lighting previews, the scene animation and the camera
//! spotlight.
/*!
 Every control runs an `async.*` command. Frame timing is the host's: its
 stats overlay shows the engine phases and its GPU work overlay the passes.
*/
class AsyncDemoPanel final : public oxygen::examples::DemoPanel {
public:
  AsyncDemoPanel(
    observer_ptr<AsyncDemoVm> vm, observer_ptr<PanelCommands> commands);
  ~AsyncDemoPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(AsyncDemoPanel)
  OXYGEN_MAKE_NON_MOVABLE(AsyncDemoPanel)

  auto DrawContents() -> void override;
  //! The tour overlay, below the UI that covers `top_inset` of the window.
  auto DrawPresentation(frame::SequenceNumber frame_sequence, float top_inset)
    -> void;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;

  auto OnLoaded() -> void override { }
  auto OnUnloaded() -> void override { }

private:
  auto DrawShowcaseControls() -> void;
  void DrawSceneInfo();
  void DrawSpotlightControls();
  auto RegisterCommands() -> void;

  observer_ptr<AsyncDemoVm> vm_;
  observer_ptr<PanelCommands> commands_;
  bool comparison_held_ { false };
};

} // namespace oxygen::examples::async
