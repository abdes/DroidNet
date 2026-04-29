//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Internal/PointShadowSetup.h>

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Types/ViewHelpers.h>

namespace oxygen::vortex::shadows::internal {
namespace {

constexpr float kMinPointNearPlane = 0.1F;
constexpr float kMinPointRange = 0.1F;
constexpr float kUePointLightShadowDepthBias = 3.0F;
constexpr float kUePointLightShadowSlopeDepthBias = 3.0F;
constexpr float kUeMaxUserShadowBias = 10.0F;

constexpr auto kPointFaceDirections = std::array {
  glm::vec3 { 1.0F, 0.0F, 0.0F },
  glm::vec3 { -1.0F, 0.0F, 0.0F },
  glm::vec3 { 0.0F, 1.0F, 0.0F },
  glm::vec3 { 0.0F, -1.0F, 0.0F },
  glm::vec3 { 0.0F, 0.0F, 1.0F },
  glm::vec3 { 0.0F, 0.0F, -1.0F },
};

constexpr auto kPointFaceUps = std::array {
  glm::vec3 { 0.0F, 0.0F, 1.0F },
  glm::vec3 { 0.0F, 0.0F, 1.0F },
  glm::vec3 { 0.0F, 0.0F, 1.0F },
  glm::vec3 { 0.0F, 0.0F, 1.0F },
  glm::vec3 { 0.0F, -1.0F, 0.0F },
  glm::vec3 { 0.0F, 1.0F, 0.0F },
};

[[nodiscard]] auto ComputePointDepthBias(const FrameLocalLightSelection& light,
  const float depth_span, const std::uint32_t resolution) -> float
{
  if (!std::isfinite(light.shadow_bias) || light.shadow_bias <= 0.0F) {
    return 0.0F;
  }

  const auto safe_depth_span = (std::max)(depth_span, kMinPointRange);
  const auto safe_resolution = (std::max)(resolution, 1U);
  const auto user_bias = std::clamp(light.shadow_bias, 0.0F, kUeMaxUserShadowBias);
  const auto bias = kUePointLightShadowDepthBias * 512.0F
    / (safe_depth_span * static_cast<float>(safe_resolution))
    * 2.0F * user_bias;
  return std::clamp(bias, 0.0F, 0.1F);
}

} // namespace

auto PointShadowSetup::BuildPointFrameBindings(
  const PreparedViewShadowInput& view_input,
  const std::span<const FrameLocalLightSelection> local_lights,
  const ConventionalShadowTargetAllocator::PointAllocation& allocation) const
  -> ShadowFrameBindings
{
  auto bindings = ShadowFrameBindings {};
  if (!allocation.surface_srv.IsValid() || view_input.resolved_view == nullptr) {
    return bindings;
  }

  const auto inverse_resolution = allocation.resolution.x > 0U
    ? 1.0F / static_cast<float>(allocation.resolution.x)
    : 0.0F;

  bindings.point_shadow_surface_handle = allocation.surface_srv;
  bindings.technique_flags = kShadowTechniquePointConventional;
  bindings.sampling_contract_flags = kShadowSamplingContractTextureCubeArray;

  auto point_shadow_index = 0U;
  for (const auto& light : local_lights) {
    if (light.kind != LocalLightKind::kPoint
      || (light.flags & kLocalLightFlagCastsShadows) == 0U) {
      continue;
    }
    if (point_shadow_index >= ShadowFrameBindings::kMaxPointShadows
      || point_shadow_index >= allocation.shadow_count) {
      break;
    }

    const auto range = (std::max)(light.range, kMinPointRange);
    const auto projection = MakeReversedZPerspectiveProjectionRH_ZO(
      glm::half_pi<float>(), 1.0F, kMinPointNearPlane, range);
    const auto depth_span = range - kMinPointNearPlane;
    const auto depth_bias
      = ComputePointDepthBias(light, depth_span, allocation.resolution.x);
    const auto world_texel_size
      = (2.0F * range) / static_cast<float>((std::max)(allocation.resolution.x, 1U));

    auto& point = bindings.point_shadows[point_shadow_index];
    for (std::size_t face_index = 0U;
         face_index < kPointFaceDirections.size(); ++face_index) {
      const auto view = glm::lookAtRH(light.position,
        light.position + kPointFaceDirections[face_index],
        kPointFaceUps[face_index]);
      point.face_light_view_projection[face_index] = projection * view;
    }
    point.position_and_inv_range
      = glm::vec4(light.position, range > 0.0F ? 1.0F / range : 0.0F);
    point.sampling_metadata0 = glm::vec4(static_cast<float>(point_shadow_index),
      inverse_resolution, world_texel_size, depth_bias);
    point.sampling_metadata1 = glm::vec4(
      (std::max)(light.shadow_normal_bias, 0.0F),
      depth_bias * kUePointLightShadowSlopeDepthBias, 1.0F, 0.0F);
    ++point_shadow_index;
  }

  bindings.point_shadow_count = point_shadow_index;
  if (bindings.point_shadow_count == 0U) {
    bindings.point_shadow_surface_handle = kInvalidShaderVisibleIndex;
    bindings.technique_flags = 0U;
    bindings.sampling_contract_flags = 0U;
  }
  return bindings;
}

} // namespace oxygen::vortex::shadows::internal
