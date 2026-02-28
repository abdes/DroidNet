//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Cooker/Pak/PakPlan.h>

namespace oxygen::content::pak {

[[nodiscard]] auto MeasureBrowseIndexPayload(
  std::span<const PakBrowseEntryPlan> entries) -> std::optional<uint64_t>;

[[nodiscard]] auto StoreBrowseIndexPayload(
  std::span<const PakBrowseEntryPlan> entries,
  std::vector<std::byte>& out_bytes) -> bool;

} // namespace oxygen::content::pak
