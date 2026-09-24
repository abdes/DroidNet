//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowProjection.h>
#include <Oxygen/Vortex/Shadows/Internal/PointShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::shadows::internal {
namespace {

  constexpr float kMinPointNearPlane = 0.1F;
  constexpr float kMinPointRange = 0.1F;
  // Oxygen's retained linear-depth profile, not UE's projected-depth bias.
  constexpr float kPointShadowDepthBiasScale = 3.0F;
  constexpr float kMaxUserShadowBias = 10.0F;

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

  [[nodiscard]] auto ComputePointDepthBias(
    const FrameLocalLightSelection& light, const float depth_span,
    const std::uint32_t resolution) -> float
  {
    if (!std::isfinite(light.shadow_bias) || light.shadow_bias <= 0.0F) {
      return 0.0F;
    }

    const auto safe_depth_span = (std::max)(depth_span, kMinPointRange);
    const auto safe_resolution = (std::max)(resolution, 1U);
    const auto user_bias
      = std::clamp(light.shadow_bias, 0.0F, kMaxUserShadowBias);
    const auto bias = kPointShadowDepthBiasScale * 512.0F
      / (safe_depth_span * static_cast<float>(safe_resolution)) * 2.0F
      * user_bias;
    return std::clamp(bias, 0.0F, 0.1F);
  }

} // namespace

auto PointShadowSetup::BuildPointRecords(
  const PreparedViewShadowInput& view_input,
  const std::span<const FrameLocalLightSelection> local_lights,
  const std::span<const ConventionalShadowTargetAllocator::LocalSelection>
    selections,
  const ConventionalShadowTargetAllocator::PointAllocation& allocation) const
  -> std::vector<CubeLocalShadowRecord>
{
  auto records = std::vector<CubeLocalShadowRecord> {};
  if (!allocation.surface_srv.IsValid()
    || view_input.resolved_view == nullptr) {
    return records;
  }

  const auto inverse_resolution = allocation.resolution.x > 0U
    ? 1.0F / static_cast<float>(allocation.resolution.x)
    : 0.0F;

  for (const auto& selection : selections) {
    const auto selection_index = selection.selection_index.get();
    const auto& light = local_lights[selection_index];
    if (!UsesCubeLocalShadow(light)
      || !HasLocalShadowInfluence(light, view_input.resolved_view.get())) {
      continue;
    }
    if (selection.slot.offset >= allocation.shadow_count) {
      break;
    }

    const auto range = light.range;
    const auto near_plane = (std::min)(kMinPointNearPlane, range * 0.01F);
    const auto projection = MakeReversedZPerspectiveProjectionRH_ZO(
      glm::half_pi<float>(), 1.0F, near_plane, range);
    const auto depth_span = range - near_plane;
    const auto depth_bias
      = ComputePointDepthBias(light, depth_span, allocation.resolution.x);
    const auto world_texel_size = (2.0F * range)
      / static_cast<float>((std::max)(allocation.resolution.x, 1U));

    auto& point = records.emplace_back();
    for (std::size_t face_index = 0U; face_index < kPointFaceDirections.size();
      ++face_index) {
      const auto view = glm::lookAtRH(light.position,
        light.position + kPointFaceDirections[face_index],
        kPointFaceUps[face_index]);
      point.face_light_view_projection[face_index] = projection * view;
    }
    point.shadow_origin_ws = light.position;
    point.near_plane_m = near_plane;
    point.far_plane_m = range;
    point.normal_bias_m = (std::max)(light.shadow_normal_bias, 0.0F);
    point.depth_bias = depth_bias;
    point.world_texel_size = world_texel_size;
    point.surface_srv = allocation.surface_srv;
    point.selection_index
      = LightSelectionIndex { static_cast<std::uint32_t>(selection_index) };
    point.first_array_layer = ShadowArrayLayer { selection.slot.offset
      * CubeLocalShadowRecord::kFaceCount };
    point.inverse_resolution = { inverse_resolution, inverse_resolution };
  }

  return records;
}

} // namespace oxygen::vortex::shadows::internal
