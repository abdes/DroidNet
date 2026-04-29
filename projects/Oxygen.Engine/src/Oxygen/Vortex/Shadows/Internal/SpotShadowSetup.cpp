//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Internal/SpotShadowSetup.h>

#include <algorithm>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Types/ViewHelpers.h>

namespace oxygen::vortex::shadows::internal {
namespace {

constexpr float kMinSpotNearPlane = 0.1F;
constexpr float kMinSpotRange = 0.1F;
constexpr float kUeSpotLightShadowDepthBias = 3.0F;
constexpr float kUeSpotLightShadowSlopeDepthBias = 3.0F;
constexpr float kUeMaxUserShadowBias = 10.0F;

[[nodiscard]] auto NormalizeOrFallback(
  const glm::vec3 direction, const glm::vec3 fallback) -> glm::vec3
{
  const auto length_sq = glm::dot(direction, direction);
  return length_sq > 1.0e-8F ? direction / std::sqrt(length_sq) : fallback;
}

[[nodiscard]] auto BuildSpotViewMatrix(
  const glm::vec3 position, const glm::vec3 direction) -> glm::mat4
{
  const auto forward = NormalizeOrFallback(
    direction, glm::vec3 { 0.0F, -1.0F, 0.0F });
  auto up = glm::vec3 { 0.0F, 1.0F, 0.0F };
  if (std::abs(glm::dot(up, forward)) > 0.98F) {
    up = glm::vec3 { 0.0F, 0.0F, 1.0F };
  }
  return glm::lookAtRH(position, position + forward, up);
}

[[nodiscard]] auto ComputeSpotDepthBias(const FrameLocalLightSelection& light,
  const float depth_span, const std::uint32_t resolution) -> float
{
  if (!std::isfinite(light.shadow_bias) || light.shadow_bias <= 0.0F) {
    return 0.0F;
  }

  const auto safe_depth_span = (std::max)(depth_span, kMinSpotRange);
  const auto safe_resolution = (std::max)(resolution, 1U);
  const auto user_bias = std::clamp(light.shadow_bias, 0.0F, kUeMaxUserShadowBias);
  const auto bias = kUeSpotLightShadowDepthBias * 512.0F
    / (safe_depth_span * static_cast<float>(safe_resolution))
    * 2.0F * user_bias;
  return std::clamp(bias, 0.0F, 0.1F);
}

[[nodiscard]] auto ResolveOuterConeCos(const FrameLocalLightSelection& light)
  -> float
{
  return std::clamp(light.outer_cone_cos, 0.001F, 0.999999F);
}

} // namespace

auto SpotShadowSetup::BuildSpotFrameBindings(
  const PreparedViewShadowInput& view_input,
  const std::span<const FrameLocalLightSelection> local_lights,
  const ConventionalShadowTargetAllocator::SpotAllocation& allocation) const
  -> ShadowFrameBindings
{
  auto bindings = ShadowFrameBindings {};
  if (!allocation.surface_srv.IsValid() || view_input.resolved_view == nullptr) {
    return bindings;
  }

  const auto inverse_resolution_x = allocation.resolution.x > 0U
    ? 1.0F / static_cast<float>(allocation.resolution.x)
    : 0.0F;
  const auto inverse_resolution_y = allocation.resolution.y > 0U
    ? 1.0F / static_cast<float>(allocation.resolution.y)
    : 0.0F;

  bindings.spot_shadow_surface_handle = allocation.surface_srv;
  bindings.technique_flags = kShadowTechniqueSpotConventional;
  bindings.sampling_contract_flags = kShadowSamplingContractTexture2DArray;

  auto spot_shadow_index = 0U;
  for (const auto& light : local_lights) {
    if (light.kind != LocalLightKind::kSpot
      || (light.flags & kLocalLightFlagCastsShadows) == 0U) {
      continue;
    }
    if (spot_shadow_index >= ShadowFrameBindings::kMaxSpotShadows
      || spot_shadow_index >= allocation.shadow_count) {
      break;
    }

    const auto range = (std::max)(light.range, kMinSpotRange);
    const auto outer_cos = ResolveOuterConeCos(light);
    const auto outer_angle = std::acos(outer_cos);
    const auto direction = NormalizeOrFallback(
      light.direction, glm::vec3 { 0.0F, -1.0F, 0.0F });
    const auto view = BuildSpotViewMatrix(light.position, direction);
    const auto projection = MakeReversedZPerspectiveProjectionRH_ZO(
      2.0F * outer_angle, 1.0F, kMinSpotNearPlane, range);
    const auto depth_span = range - kMinSpotNearPlane;
    const auto depth_bias
      = ComputeSpotDepthBias(light, depth_span, allocation.resolution.x);
    const auto outer_sine
      = std::sqrt((std::max)(0.0F, 1.0F - outer_cos * outer_cos));
    const auto outer_tangent = outer_sine / (std::max)(outer_cos, 1.0e-4F);
    const auto world_texel_size
      = (2.0F * range * outer_tangent)
      / static_cast<float>((std::max)(allocation.resolution.x, 1U));

    auto& spot = bindings.spot_shadows[spot_shadow_index];
    spot.light_view_projection = projection * view;
    spot.position_and_inv_range
      = glm::vec4(light.position, range > 0.0F ? 1.0F / range : 0.0F);
    spot.direction_and_bias = glm::vec4(direction, depth_bias);
    spot.sampling_metadata0 = glm::vec4(static_cast<float>(spot_shadow_index),
      inverse_resolution_x, inverse_resolution_y, world_texel_size);
    spot.sampling_metadata1 = glm::vec4(kMinSpotNearPlane, range,
      depth_bias * kUeSpotLightShadowSlopeDepthBias,
      (std::max)(light.shadow_normal_bias, 0.0F));
    ++spot_shadow_index;
  }

  bindings.spot_shadow_count = spot_shadow_index;
  if (bindings.spot_shadow_count == 0U) {
    bindings.spot_shadow_surface_handle = kInvalidShaderVisibleIndex;
    bindings.technique_flags = 0U;
    bindings.sampling_contract_flags = 0U;
  }
  return bindings;
}

} // namespace oxygen::vortex::shadows::internal
