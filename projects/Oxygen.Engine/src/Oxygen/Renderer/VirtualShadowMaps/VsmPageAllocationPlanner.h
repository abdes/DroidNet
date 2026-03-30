//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerSeam.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

class VsmPageAllocationPlanner {
public:
  struct Result {
    VsmPageAllocationPlan plan {};
    VsmPageAllocationSnapshot snapshot {};

    auto operator==(const Result&) const -> bool = default;
  };

  OXGN_RNDR_API VsmPageAllocationPlanner() = default;
  OXGN_RNDR_API ~VsmPageAllocationPlanner();

  OXGN_RNDR_NDAPI auto Build(const VsmCacheManagerSeam& seam,
    const VsmExtractedCacheFrame* previous_frame,
    VsmCacheDataState cache_data_state,
    std::span<const VsmPageRequest> requests) const -> Result;
};

} // namespace oxygen::renderer::vsm
