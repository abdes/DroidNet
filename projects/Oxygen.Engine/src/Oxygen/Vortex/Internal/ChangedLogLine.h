//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <unordered_map>
#include <utility>

#include <Oxygen/Core/Types/View.h>

namespace oxygen::vortex::internal {

//! The last report logged for each view, so a per-frame report is logged
//! only when it changes.
class ChangedLogLine {
public:
  //! True when `line` differs from the last one recorded for `view`, which
  //! it then replaces.
  [[nodiscard]] auto Update(const ViewId view, std::string line) -> bool
  {
    const auto [entry, inserted] = last_.try_emplace(view);
    if (!inserted && entry->second == line) {
      return false;
    }
    entry->second = std::move(line);
    return true;
  }

  auto Forget(const ViewId view) -> void { last_.erase(view); }

private:
  std::unordered_map<ViewId, std::string> last_;
};

} // namespace oxygen::vortex::internal
