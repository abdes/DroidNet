//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::pak {

enum class PakPlanMode : uint8_t {
  kFull,
  kPatch,
};

OXGN_COOK_NDAPI auto to_string(PakPlanMode value) noexcept -> std::string_view;

struct PakPlanPolicy {
  PakPlanMode mode = PakPlanMode::kFull;
  bool requires_base_catalogs = false;
  bool emits_manifest = false;
};

OXGN_COOK_NDAPI auto DerivePakPlanPolicy(
  const PakBuildRequest& request) noexcept -> PakPlanPolicy;

} // namespace oxygen::content::pak
