//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/Pak/PakPlanPolicy.h>

namespace oxygen::content::pak {

auto to_string(const PakPlanMode value) noexcept -> std::string_view
{
  switch (value) {
  case PakPlanMode::kFull:
    return "Full";
  case PakPlanMode::kPatch:
    return "Patch";
  }

  return "__NotSupported__";
}

auto DerivePakPlanPolicy(const PakBuildRequest& request) noexcept
  -> PakPlanPolicy
{
  const auto plan_mode = request.mode == BuildMode::kPatch ? PakPlanMode::kPatch
                                                           : PakPlanMode::kFull;

  return PakPlanPolicy {
    .mode = plan_mode,
    .requires_base_catalogs = plan_mode == PakPlanMode::kPatch,
    .emits_manifest = (plan_mode == PakPlanMode::kPatch)
      || (request.mode == BuildMode::kFull
        && request.options.emit_manifest_in_full),
  };
}

} // namespace oxygen::content::pak
