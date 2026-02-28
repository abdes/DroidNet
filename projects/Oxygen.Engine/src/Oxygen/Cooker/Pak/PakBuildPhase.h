//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::pak {

enum class PakBuildPhase : uint8_t {
  kRequestValidation,
  kPlanning,
  kWriting,
  kManifest,
  kFinalize,
};

OXGN_COOK_NDAPI auto to_string(PakBuildPhase value) noexcept
  -> std::string_view;

} // namespace oxygen::content::pak
