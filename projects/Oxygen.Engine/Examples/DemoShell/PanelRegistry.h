//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/DemoPanel.h"

namespace oxygen::examples {

//! Errors reported by the panel registry.
enum class PanelRegistryError {
  kNullPanel,
  kDuplicateName,
  kPanelNotFound,
};

//! Registry of panels hosted by the demo shell.
/*!
 Owns a list of non-owning panel references and tracks which panel is currently
 active in the SidePanel window.

 ### Key Features

 - **Single Active Panel**: Only one panel can be active at a time.
 - **Fast Lookup**: Name-based selection using cached names.
 - **Non-owning**: Uses `observer_ptr` to avoid ownership coupling.
*/
class PanelRegistry {
public:
  //! Panel entry stored in the registry.
  struct PanelEntry {
    std::string name;
    observer_ptr<DemoPanel> panel { nullptr };
  };

  //! Register a panel instance.
  auto RegisterPanel(observer_ptr<DemoPanel> panel)
    -> std::expected<void, PanelRegistryError>;

  //! Activate a panel by name.
  auto SetActivePanelByName(std::string_view name)
    -> std::expected<void, PanelRegistryError>;

  //! Clears the active panel selection.
  auto ClearActivePanel() noexcept -> void;

  //! Returns the currently active panel, if any.
  [[nodiscard]] auto GetActivePanel() const noexcept -> observer_ptr<DemoPanel>;

  //! Returns the active panel name, or empty if none.
  [[nodiscard]] auto GetActivePanelName() const noexcept -> std::string_view;

  //! Returns a view of all registered panels.
  [[nodiscard]] auto GetPanels() const noexcept -> std::span<const PanelEntry>;

private:
  std::vector<PanelEntry> panels_ {};
  std::optional<std::size_t> active_index_ {};
};

} // namespace oxygen::examples
