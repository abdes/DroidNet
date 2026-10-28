//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>
#include <string_view>

#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content {

//! What a content tool's `versions` command prints.
struct CookedVersionsReport {
  std::string_view program;
  std::string_view version;
  //! Whether the tool cooks content, and so reports its importers' revisions.
  bool importers = false;
  //! Whether to print one JSON object rather than text.
  bool json = false;
};

//! Prints the versions cooked content depends on, from `PakFormatVersions.inc`:
//! every cooked format and asset, then, for a tool that cooks, every importer.
/*!
 The JSON object holds `program`, `version`, then `formats`, `assets` and
 `importers`, each an object from a name to its version.
*/
OXGN_COOK_API auto PrintCookedVersions(
  std::ostream& out, const CookedVersionsReport& report) -> void;

} // namespace oxygen::content
