//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PatchManifest.h>

namespace oxygen::content::pak {

struct PakBuildResult {
  data::PakCatalog output_catalog;
  std::optional<data::PatchManifest> patch_manifest;
  uint64_t file_size = 0;
  uint32_t pak_crc32 = 0;
  std::vector<PakDiagnostic> diagnostics;
  PakBuildSummary summary {};
  PakBuildTelemetry telemetry {};
};

} // namespace oxygen::content::pak
