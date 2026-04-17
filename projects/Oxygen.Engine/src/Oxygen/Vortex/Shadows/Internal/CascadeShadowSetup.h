//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowFrameData.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::shadows::internal {

class CascadeShadowSetup {
public:
  OXGN_VRTX_API CascadeShadowSetup() = default;
  OXGN_VRTX_API ~CascadeShadowSetup() = default;

  CascadeShadowSetup(const CascadeShadowSetup&) = delete;
  auto operator=(const CascadeShadowSetup&) -> CascadeShadowSetup& = delete;
  CascadeShadowSetup(CascadeShadowSetup&&) = delete;
  auto operator=(CascadeShadowSetup&&) -> CascadeShadowSetup& = delete;

  [[nodiscard]] OXGN_VRTX_API auto BuildDirectionalFrameData(
    const PreparedViewShadowInput& view_input,
    const FrameDirectionalLightSelection& directional_light,
    const ConventionalShadowTargetAllocator::DirectionalAllocation& allocation) const
    -> DirectionalShadowFrameData;
};

} // namespace oxygen::vortex::shadows::internal
