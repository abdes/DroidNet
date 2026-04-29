//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h>

namespace oxygen::vortex {

auto MakeInvalidOcclusionFrameResults(
  const OcclusionFallbackReason reason) noexcept -> OcclusionFrameResults
{
  return OcclusionFrameResults {
    .fallback_reason = reason,
  };
}

} // namespace oxygen::vortex
