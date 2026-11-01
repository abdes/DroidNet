//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class ContentVm;
struct ContentPanelNavigation;
struct CookedSourceRow;

//! The Mounts tab: the cooked sources in precedence order, the details of
//! the selected one and why it is refused, missing or unreadable.
class ContentMountsTab final {
public:
  ContentMountsTab(observer_ptr<ContentVm> vm,
    observer_ptr<PanelCommands> commands,
    observer_ptr<ContentPanelNavigation> navigation);

  auto Draw() -> void;

private:
  auto DrawToolbar() -> void;
  auto DrawTable() -> void;
  auto DrawRow(const CookedSourceRow& row, std::size_t index) -> void;
  auto DrawDetails(const CookedSourceRow& row, std::size_t index) -> void;
  //! The red box of a refused, missing or unreadable source.
  auto DrawFindings(const CookedSourceRow& row, std::size_t index) -> void;
  //! What the refused pak of row `index` requires, met or not.
  auto DrawRequirements(const CookedSourceRow& row, std::size_t index) -> void;
  auto DrawActions(const CookedSourceRow& row, std::size_t index) -> void;
  auto DrawAssets(std::size_t index) -> void;
  auto DrawSourceFolders() -> void;

  observer_ptr<ContentVm> vm_;
  observer_ptr<PanelCommands> commands_;
  observer_ptr<ContentPanelNavigation> navigation_;
  std::optional<std::size_t> selected_;
  std::string asset_search_;
};

} // namespace oxygen::examples::ui
