//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Runtime/Internal/PipelineSettings.h"

namespace oxygen::examples::internal {

auto PipelineSettingsDraft::Commit() -> CommitResult
{
  CommitResult result {
    .settings = static_cast<const PipelineSettings&>(*this),
    .auto_exposure_reset_ev = auto_exposure_reset_pending
      ? std::optional<float> { auto_exposure_reset_ev }
      : std::nullopt,
  };
  auto_exposure_reset_pending = false;
  dirty = false;
  return result;
}

} // namespace oxygen::examples::internal
