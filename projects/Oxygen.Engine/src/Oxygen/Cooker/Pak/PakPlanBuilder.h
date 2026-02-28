//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>

namespace oxygen::content::pak {

class PakPlanBuilder final {
public:
  struct BuildResult {
    std::optional<PakPlan> plan;
    std::vector<PakDiagnostic> diagnostics;
    PakBuildSummary summary {};
    std::optional<std::chrono::microseconds> planning_duration;
  };

  [[nodiscard]] auto Build(const PakBuildRequest& request) const -> BuildResult;
};

} // namespace oxygen::content::pak
