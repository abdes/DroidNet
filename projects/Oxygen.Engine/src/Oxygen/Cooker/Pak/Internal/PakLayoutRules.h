//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstdint>
#include <ranges>

#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Data/PakFormat_core.h>

//! Layout rules of a pak the runtime reads by position, which the validator
//! checks of a plan and the writer refuses a plan that breaks.
namespace oxygen::content::pak::internal {

//! Whether each directory record states its own position: the runtime opens a
//! record only where its `entry_offset` says it lies.
[[nodiscard]] inline auto DirectoryRecordsInPlace(
  const PakDirectoryPlan& directory) -> bool
{
  constexpr auto kRecordSize
    = uint64_t { sizeof(data::pak::core::AssetDirectoryEntry) };
  const auto holds_records
    = directory.size_bytes == directory.entries.size() * kRecordSize;
  const auto records_in_place
    = std::ranges::all_of(std::views::enumerate(directory.entries),
      [&directory](const auto& pair) -> bool {
        const auto& [index, entry] = pair;
        return entry.entry_offset
          == directory.offset + (static_cast<uint64_t>(index) * kRecordSize);
      });
  return holds_records && records_in_place;
}

//! Whether the footer ends the file: the runtime reads it from the end.
[[nodiscard]] inline auto FooterEndsFile(const PakPlan& plan) -> bool
{
  return plan.Footer().offset + plan.Footer().size_bytes
    == plan.PlannedFileSize();
}

} // namespace oxygen::content::pak::internal
