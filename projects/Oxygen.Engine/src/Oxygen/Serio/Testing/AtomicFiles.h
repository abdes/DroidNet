//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <filesystem>

namespace oxygen::serio::testing {

//! Whether an atomic write left its temporary sibling in `folder`.
/*!
 `WriteFileAtomically` and `AtomicFileWriter` stream into a `.oxygen-` sibling
 of the destination and remove it whether or not they publish.
*/
[[nodiscard]] inline auto HasTemporarySibling(
  const std::filesystem::path& folder) -> bool
{
  return std::ranges::any_of(std::filesystem::directory_iterator(folder),
    [](const std::filesystem::directory_entry& entry) -> bool {
      return entry.path().filename().string().starts_with(".oxygen-");
    });
}

} // namespace oxygen::serio::testing
