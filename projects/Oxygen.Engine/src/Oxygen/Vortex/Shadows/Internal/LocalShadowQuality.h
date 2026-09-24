//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowProjection.h>

namespace oxygen::vortex::shadows::internal {

struct LocalShadowQuality {
  std::uint32_t resolution { 0U };
  float strength { 0.0F };
};

//! Quantized texel-density policy with a shrink guard band and smooth fading.
[[nodiscard]] inline auto SelectLocalShadowQuality(const float desired_texels,
  const std::uint32_t ceiling, const std::uint32_t previous_resolution = 0U)
  -> LocalShadowQuality
{
  constexpr float kMinimumTexels = 32.0F;
  constexpr float kFullStrengthTexels = 64.0F;
  if (ceiling == 0U || desired_texels <= kMinimumTexels) {
    return {};
  }
  const auto bounded
    = std::clamp(desired_texels, kMinimumTexels, static_cast<float>(ceiling));
  auto resolution = std::bit_floor(static_cast<std::uint32_t>(bounded));
  if (previous_resolution != 0U && previous_resolution <= ceiling
    && desired_texels >= static_cast<float>(previous_resolution) * 0.75F
    && desired_texels < static_cast<float>(previous_resolution) * 2.0F) {
    resolution = previous_resolution;
  }
  auto strength = std::clamp(
    (desired_texels - kMinimumTexels) / (kFullStrengthTexels - kMinimumTexels),
    0.0F, 1.0F);
  strength = strength * strength * (3.0F - 2.0F * strength);
  return { resolution, strength };
}

[[nodiscard]] inline auto EvaluateLocalShadowQuality(
  const FrameLocalLightSelection& light, const ResolvedView* view,
  const std::uint32_t ceiling, const std::uint32_t previous_resolution = 0U)
  -> LocalShadowQuality
{
  if (view == nullptr) {
    return { ceiling, 1.0F };
  }
  const auto camera_delta = view->CameraPosition() - light.position;
  if (!view->IsOrthographic()
    && glm::dot(camera_delta, camera_delta) <= light.range * light.range) {
    return { ceiling, 1.0F };
  }
  const auto projection = view->StableProjectionMatrix();
  const auto viewport = view->Viewport();
  const auto screen_scale = 0.5F
    * (std::max)(viewport.width * std::abs(projection[0][0]),
      viewport.height * std::abs(projection[1][1]));
  auto radius_pixels = screen_scale * light.range;
  if (!view->IsOrthographic()) {
    const auto position = view->ViewMatrix() * glm::vec4(light.position, 1.0F);
    // Near-side depth keeps lights crossing the near plane at full quality.
    const auto depth = (std::max)(-position.z - light.range,
      (std::max)(view->NearPlane(), 1.0e-4F));
    radius_pixels /= depth;
  }
  constexpr float kPointTexelsPerPixel = 1.27324F;
  const auto density = UsesCubeLocalShadow(light) ? kPointTexelsPerPixel
                                                  : 2.0F * kPointTexelsPerPixel;
  return SelectLocalShadowQuality(
    radius_pixels * density, ceiling, previous_resolution);
}

} // namespace oxygen::vortex::shadows::internal
