//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::shadows::internal {

class PointShadowSetup {
public:
  OXGN_VRTX_API PointShadowSetup() = default;
  OXGN_VRTX_API ~PointShadowSetup() = default;

  PointShadowSetup(const PointShadowSetup&) = delete;
  auto operator=(const PointShadowSetup&) -> PointShadowSetup& = delete;
  PointShadowSetup(PointShadowSetup&&) = delete;
  auto operator=(PointShadowSetup&&) -> PointShadowSetup& = delete;

  [[nodiscard]] OXGN_VRTX_API auto BuildPointFrameBindings(
    const PreparedViewShadowInput& view_input,
    std::span<const FrameLocalLightSelection> local_lights,
    const ConventionalShadowTargetAllocator::PointAllocation& allocation) const
    -> ShadowFrameBindings;
};

} // namespace oxygen::vortex::shadows::internal
