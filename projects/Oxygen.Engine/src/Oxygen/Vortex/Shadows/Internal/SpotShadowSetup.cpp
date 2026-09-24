//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowProjection.h>
#include <Oxygen/Vortex/Shadows/Internal/SpotShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/ProjectedLocalShadowRecord.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::shadows::internal {
namespace {

  constexpr float kMinSpotNearPlane = 0.1F;
  constexpr float kMinSpotRange = 0.1F;
  constexpr float kUeSpotLightShadowDepthBias = 3.0F;
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
    const auto forward
      = NormalizeOrFallback(direction, glm::vec3 { 0.0F, -1.0F, 0.0F });
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
    const auto user_bias
      = std::clamp(light.shadow_bias, 0.0F, kUeMaxUserShadowBias);
    const auto bias = kUeSpotLightShadowDepthBias * 512.0F
      / (safe_depth_span * static_cast<float>(safe_resolution)) * 2.0F
      * user_bias;
    return std::clamp(bias, 0.0F, 0.1F);
  }

  [[nodiscard]] auto ResolveOuterConeCos(const FrameLocalLightSelection& light)
    -> float
  {
    return std::cos(light.outer_cone_half_angle_radians);
  }

} // namespace

auto SpotShadowSetup::BuildSpotRecords(
  const PreparedViewShadowInput& view_input,
  const std::span<const FrameLocalLightSelection> local_lights,
  const std::span<const ConventionalShadowTargetAllocator::LocalSelection>
    selections,
  const ConventionalShadowTargetAllocator::SpotAllocation& allocation) const
  -> std::vector<ProjectedLocalShadowRecord>
{
  auto records = std::vector<ProjectedLocalShadowRecord> {};
  if (!allocation.surface_srv.IsValid()
    || view_input.resolved_view == nullptr) {
    return records;
  }

  const auto inverse_resolution_x = allocation.resolution.x > 0U
    ? 1.0F / static_cast<float>(allocation.resolution.x)
    : 0.0F;
  const auto inverse_resolution_y = allocation.resolution.y > 0U
    ? 1.0F / static_cast<float>(allocation.resolution.y)
    : 0.0F;

  for (const auto& selection : selections) {
    const auto selection_index = selection.selection_index.get();
    const auto& light = local_lights[selection_index];
    if (UsesCubeLocalShadow(light)
      || !HasLocalShadowInfluence(light, view_input.resolved_view.get())) {
      continue;
    }
    if (selection.slot.offset >= allocation.shadow_count) {
      break;
    }

    const auto range = light.range;
    const auto near_plane = (std::min)(kMinSpotNearPlane, range * 0.01F);
    const auto outer_cos = ResolveOuterConeCos(light);
    const auto outer_angle = light.outer_cone_half_angle_radians;
    const auto direction
      = NormalizeOrFallback(light.direction, glm::vec3 { 0.0F, -1.0F, 0.0F });
    const auto view = BuildSpotViewMatrix(light.position, direction);
    const auto projection = MakeReversedZPerspectiveProjectionRH_ZO(
      2.0F * outer_angle, 1.0F, near_plane, range);
    const auto depth_span = range - near_plane;
    const auto depth_bias
      = ComputeSpotDepthBias(light, depth_span, allocation.resolution.x);
    const auto outer_sine
      = std::sqrt((std::max)(0.0F, 1.0F - outer_cos * outer_cos));
    const auto outer_tangent = outer_sine / outer_cos;
    const auto world_texel_size = (2.0F * range * outer_tangent)
      / static_cast<float>((std::max)(allocation.resolution.x, 1U));

    auto& spot = records.emplace_back();
    spot.light_view_projection = projection * view;
    spot.shadow_origin_ws = light.position;
    spot.near_plane_m = near_plane;
    spot.far_plane_m = range;
    spot.normal_bias_m = (std::max)(light.shadow_normal_bias, 0.0F);
    spot.depth_bias = depth_bias;
    spot.world_texel_size = world_texel_size;
    spot.surface_srv = allocation.surface_srv;
    spot.selection_index
      = LightSelectionIndex { static_cast<std::uint32_t>(selection_index) };
    spot.array_layer = ShadowArrayLayer { selection.slot.offset };
    spot.inverse_resolution = { inverse_resolution_x, inverse_resolution_y };
  }

  return records;
}

} // namespace oxygen::vortex::shadows::internal
