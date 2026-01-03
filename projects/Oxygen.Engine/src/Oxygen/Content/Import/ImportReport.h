//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <Oxygen/Data/SourceKey.h>

#include <Oxygen/Content/Import/ImportDiagnostics.h>

namespace oxygen::content::import {

//! Summary of an import to a cooked container.
struct ImportReport final {
  std::filesystem::path cooked_root;
  data::SourceKey source_key { std::array<uint8_t, 16> {} };

  //! Diagnostics (warnings/errors) emitted during import.
  std::vector<ImportDiagnostic> diagnostics;

  //! Count of assets written (by type) for quick UI.
  uint32_t materials_written = 0;
  uint32_t geometry_written = 0;
  uint32_t scenes_written = 0;

  //! True if the cook completed and emitted an index.
  bool success = false;
};

} // namespace oxygen::content::import
