//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakCatalog.h>

namespace oxygen::content::pak {

class PakPlanBuilder final {
public:
  struct BuildResult {
    std::optional<PakPlan> plan;
    data::PakCatalog output_catalog {};
    std::vector<PakDiagnostic> diagnostics;
    PakBuildSummary summary {};
  };

  OXGN_COOK_NDAPI auto Build(const PakBuildRequest& request) const
    -> BuildResult;
};

} // namespace oxygen::content::pak
