//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuildResult.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::pak {

class PakBuilder {
public:
  PakBuilder() noexcept = default;
  ~PakBuilder() noexcept = default;

  OXYGEN_MAKE_NON_COPYABLE(PakBuilder)
  OXYGEN_DEFAULT_MOVABLE(PakBuilder)

  OXGN_COOK_NDAPI auto Build(const PakBuildRequest& request) noexcept
    -> Result<PakBuildResult>;
};

} // namespace oxygen::content::pak
