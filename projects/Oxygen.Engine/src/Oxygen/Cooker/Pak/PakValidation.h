//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanPolicy.h>

namespace oxygen::content::pak {

class PakValidation final {
public:
  struct Result {
    bool success = true;
    std::vector<PakDiagnostic> diagnostics;
  };

  [[nodiscard]] static auto Validate(const PakPlan& plan,
    const PakPlanPolicy& policy, const PakBuildRequest& request) -> Result;
};

} // namespace oxygen::content::pak
