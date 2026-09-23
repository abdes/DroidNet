//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <numbers>

#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::shadows::internal {

//! The disk swept by a finite cone cannot use a center-apex projection.
//! A hemispherical cone likewise requires the existing six-face coverage.
[[nodiscard]] inline auto UsesCubeLocalShadow(
  const FrameLocalLightSelection& light) -> bool
{
  return light.kind == LocalLightKind::kPoint || light.source_radius > 0.0F
    || light.outer_cone_half_angle_radians == std::numbers::pi_v<float> / 2.0F;
}

[[nodiscard]] inline auto HasLocalShadowInfluence(
  const FrameLocalLightSelection& light) -> bool
{
  return light.range > 0.0F
    && (light.flags & kLocalLightFlagCastsShadows) != 0U;
}

} // namespace oxygen::vortex::shadows::internal
