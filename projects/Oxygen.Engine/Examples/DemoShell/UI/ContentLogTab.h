//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include "DemoShell/UI/ContentLog.h"

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class ContentVm;
struct ContentPanelNavigation;

//! The Log tab: why something did not load, by code.
/*!
 Severity chips, a filter, the rows with their code, time and message, and
 the details of the selected row; "Go to the mount" opens the Mounts tab on
 the cooked source the row concerns.
*/
class ContentLogTab final {
public:
  ContentLogTab(observer_ptr<ContentVm> vm,
    observer_ptr<PanelCommands> commands,
    observer_ptr<ContentPanelNavigation> navigation);

  auto Draw() -> void;

private:
  auto DrawToolbar() -> void;
  auto DrawRows() -> void;
  auto DrawDetails(const ContentLogEntry& entry) -> void;

  observer_ptr<ContentVm> vm_;
  observer_ptr<PanelCommands> commands_;
  observer_ptr<ContentPanelNavigation> navigation_;
  LogFilter filter_;
  std::optional<std::uint64_t> selected_;
};

} // namespace oxygen::examples::ui
