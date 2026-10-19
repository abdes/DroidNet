//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Loose/Inspection.h>

namespace oxygen::content::import {
struct ImportRequest;
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::internal {

//! A cooked root a job reads: its own output root or one of its cooked context
//! roots (a loose cooked root or a pak), with its index when it holds one.
struct MountedCookedSource final {
  std::filesystem::path root;
  std::optional<lc::Inspection> inspection;
};

//! Load the cooked roots a job reads, lowest precedence first: the job's
//! output root, then its cooked context roots in order. A root whose index
//! fails to load reports `<diagnostic_prefix>index_load_failed` and is kept
//! without an index.
[[nodiscard]] auto LoadMountedCookedSources(ImportSession& session,
  const ImportRequest& request, std::string_view diagnostic_prefix)
  -> std::vector<MountedCookedSource>;

} // namespace oxygen::content::import::internal
