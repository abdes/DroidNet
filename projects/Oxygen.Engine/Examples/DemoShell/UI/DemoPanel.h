//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

namespace oxygen::examples {

//! Base interface for demo UI panels.
/*!
 Defines the minimal contract for panels hosted by the demo shell side panel.

 ### Key Features

 - **Single Responsibility**: Each panel renders its own content.
 - **Reusable**: Any demo can implement this interface to plug in.

 @see DemoShellUi, PanelRegistry
*/
class DemoPanel {
public:
  DemoPanel() = default;
  virtual ~DemoPanel() = default;

  DemoPanel(const DemoPanel&) = delete;
  auto operator=(const DemoPanel&) -> DemoPanel& = delete;
  DemoPanel(DemoPanel&&) = default;
  auto operator=(DemoPanel&&) -> DemoPanel& = default;

  //! Returns the display name of the panel.
  [[nodiscard]] virtual auto GetName() const noexcept -> std::string_view = 0;

  //! Draws the panel content inside the shared SidePanel window.
  virtual auto DrawContents() -> void = 0;

  //! Called when the panel is registered with the demo shell.
  virtual auto OnRegistered() -> void { }

  //! Called when the panel becomes the active panel.
  virtual auto OnLoaded() -> void = 0;

  //! Called when the panel is deactivated or the shell is shutting down.
  virtual auto OnUnloaded() -> void = 0;

  //! Preferred width (in pixels) for the SidePanel when this panel is active.
  [[nodiscard]] virtual auto GetPreferredWidth() const noexcept -> float
  {
    return 420.0F;
  }

  //! Returns an icon string suitable for icon fonts (empty if none).
  [[nodiscard]] virtual auto GetIcon() const noexcept -> std::string_view
  {
    return {};
  }
};

} // namespace oxygen::examples
