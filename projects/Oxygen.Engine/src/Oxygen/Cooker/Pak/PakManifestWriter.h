//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PatchManifest.h>

namespace oxygen::content::pak {

class PakManifestWriter final {
public:
  struct WriteResult final {
    std::optional<data::PatchManifest> manifest;
    std::vector<PakDiagnostic> diagnostics;
    std::optional<std::chrono::microseconds> manifest_duration;
  };

  OXGN_COOK_NDAPI auto Write(const PakBuildRequest& request,
    const PakPlan& plan, uint32_t pak_crc32) const -> WriteResult;
};

} // namespace oxygen::content::pak
