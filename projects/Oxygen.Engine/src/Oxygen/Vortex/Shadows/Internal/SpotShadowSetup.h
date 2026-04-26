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

class SpotShadowSetup {
public:
  OXGN_VRTX_API SpotShadowSetup() = default;
  OXGN_VRTX_API ~SpotShadowSetup() = default;

  SpotShadowSetup(const SpotShadowSetup&) = delete;
  auto operator=(const SpotShadowSetup&) -> SpotShadowSetup& = delete;
  SpotShadowSetup(SpotShadowSetup&&) = delete;
  auto operator=(SpotShadowSetup&&) -> SpotShadowSetup& = delete;

  [[nodiscard]] OXGN_VRTX_API auto BuildSpotFrameBindings(
    const PreparedViewShadowInput& view_input,
    std::span<const FrameLocalLightSelection> local_lights,
    const ConventionalShadowTargetAllocator::SpotAllocation& allocation) const
    -> ShadowFrameBindings;
};

} // namespace oxygen::vortex::shadows::internal
