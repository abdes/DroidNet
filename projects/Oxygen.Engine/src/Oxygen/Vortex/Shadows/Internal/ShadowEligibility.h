//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::shadows::internal {

[[nodiscard]] inline auto HasShadowEnergy(
  const float intensity, const glm::vec3 color) noexcept -> bool
{
  return intensity > 0.0F
    && (color.x > 0.0F || color.y > 0.0F || color.z > 0.0F);
}

//! Shared admission decision for allocation, rendering and shadow references.
[[nodiscard]] inline auto HasLocalShadowInfluence(
  const FrameLocalLightSelection& light, const ResolvedView* view = nullptr)
  -> bool
{
  return light.range > 0.0F && (light.flags & kLocalLightFlagCastsShadows) != 0U
    && HasShadowEnergy(light.luminous_flux_lm, light.color)
    && (view == nullptr
      || view->GetFrustum().IntersectsSphere(light.position, light.range));
}

[[nodiscard]] inline auto HasDirectionalShadowInfluence(
  const FrameDirectionalLightSelection& light) noexcept -> bool
{
  return (light.shadow_flags & kDirectionalLightShadowFlagCastsShadows) != 0U
    && HasShadowEnergy(light.illuminance_lux, light.color);
}

[[nodiscard]] inline auto NeedsContactShadows(
  const FrameLightSelection& selection, const ResolvedView* view) -> bool
{
  return std::ranges::any_of(selection.directional_lights, [](const auto& light) {
    return HasDirectionalShadowInfluence(light)
      && (light.shadow_flags & kLightFlagContactShadows) != 0U;
  }) || std::ranges::any_of(selection.local_lights, [view](const auto& light) {
    return HasLocalShadowInfluence(light, view)
      && (light.flags & kLightFlagContactShadows) != 0U;
  });
}

} // namespace oxygen::vortex::shadows::internal
