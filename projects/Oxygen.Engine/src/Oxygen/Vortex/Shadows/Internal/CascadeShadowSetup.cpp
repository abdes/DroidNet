//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace oxygen::vortex::shadows::internal {

namespace {

constexpr float kMinCascadeSpan = 0.1F;
constexpr float kLightPullbackPadding = 32.0F;
constexpr float kShadowDepthPadding = 32.0F;

struct CascadeMatrixData {
  glm::mat4 light_view_projection { 1.0F };
  float world_texel_size { 0.0F };
};

auto ResolveSafeLightDirection(const glm::vec3 direction) -> glm::vec3
{
  const auto length_sq = glm::dot(direction, direction);
  return length_sq > 1.0e-6F ? glm::normalize(direction)
                             : glm::vec3 { 0.0F, -1.0F, 0.0F };
}

auto ExtractFrustumCornersWorld(const ResolvedView& resolved_view)
  -> std::array<glm::vec3, 8>
{
  constexpr std::array<glm::vec3, 8> kNdcCorners {
    glm::vec3 { -1.0F, -1.0F, 0.0F },
    glm::vec3 { 1.0F, -1.0F, 0.0F },
    glm::vec3 { 1.0F, 1.0F, 0.0F },
    glm::vec3 { -1.0F, 1.0F, 0.0F },
    glm::vec3 { -1.0F, -1.0F, 1.0F },
    glm::vec3 { 1.0F, -1.0F, 1.0F },
    glm::vec3 { 1.0F, 1.0F, 1.0F },
    glm::vec3 { -1.0F, 1.0F, 1.0F },
  };

  auto corners = std::array<glm::vec3, 8> {};
  const auto inverse_view_projection = resolved_view.InverseViewProjection();
  for (std::size_t i = 0; i < kNdcCorners.size(); ++i) {
    const auto clip
      = inverse_view_projection * glm::vec4(kNdcCorners[i], 1.0F);
    corners[i] = glm::vec3(clip) / clip.w;
  }
  return corners;
}

auto ComputeAccumulatedScale(const float exponent,
  const std::uint32_t split_index, const std::uint32_t cascade_count) -> float
{
  if (cascade_count == 0U) {
    return 1.0F;
  }

  const auto safe_exponent = std::isfinite(exponent) && exponent > 0.0F
    ? exponent
    : 1.0F;
  auto current_scale = 1.0F;
  auto total_scale = 0.0F;
  auto accumulated = 0.0F;
  for (std::uint32_t i = 0U; i < cascade_count; ++i) {
    if (i < split_index) {
      accumulated += current_scale;
    }
    total_scale += current_scale;
    current_scale *= safe_exponent;
  }
  return total_scale > 0.0F ? accumulated / total_scale : 1.0F;
}

auto ResolveCascadeEnd(const FrameDirectionalLightSelection& directional_light,
  const std::uint32_t cascade_index, const std::uint32_t cascade_count,
  const float near_plane, const float far_plane) -> float
{
  const auto max_shadow_distance = std::isfinite(directional_light.max_shadow_distance)
      && directional_light.max_shadow_distance > near_plane
    ? directional_light.max_shadow_distance
    : scene::kDefaultDirectionalMaxShadowDistance;
  const auto shadow_far = (std::min)(far_plane, max_shadow_distance);

  auto authored_end = shadow_far;
  if (directional_light.cascade_split_mode
    == FrameDirectionalCsmSplitMode::kManualDistances) {
    const auto authored_index = (std::min)(cascade_index,
      static_cast<std::uint32_t>(directional_light.cascade_distances.size() - 1U));
    authored_end = directional_light.cascade_distances[authored_index];
  } else {
    const auto split_scale = ComputeAccumulatedScale(
      directional_light.distribution_exponent, cascade_index + 1U,
      cascade_count);
    authored_end = near_plane + split_scale * (shadow_far - near_plane);
  }

  const auto last_cascade = cascade_index + 1U == cascade_count;
  const auto clamped_far = last_cascade ? shadow_far : authored_end;
  return (std::max)(near_plane + kMinCascadeSpan,
    (std::min)(clamped_far, shadow_far));
}

auto BuildCascadeMatrix(const ResolvedView& resolved_view,
  const glm::vec3 light_direction, const float split_near,
  const float split_far, const std::uint32_t shadow_resolution) -> CascadeMatrixData
{
  const auto frustum_corners = ExtractFrustumCornersWorld(resolved_view);
  const auto camera_near = resolved_view.NearPlane();
  const auto camera_far = resolved_view.FarPlane();
  const auto depth_span = (std::max)(camera_far - camera_near, kMinCascadeSpan);
  const auto near_t = std::clamp((split_near - camera_near) / depth_span, 0.0F, 1.0F);
  const auto far_t = std::clamp((split_far - camera_near) / depth_span, 0.0F, 1.0F);

  auto slice_corners = std::array<glm::vec3, 8> {};
  for (std::size_t i = 0; i < 4U; ++i) {
    const auto& near_corner = frustum_corners[i];
    const auto& far_corner = frustum_corners[i + 4U];
    const auto ray = far_corner - near_corner;
    slice_corners[i] = near_corner + ray * near_t;
    slice_corners[i + 4U] = near_corner + ray * far_t;
  }

  auto slice_center = glm::vec3 { 0.0F };
  for (const auto& corner : slice_corners) {
    slice_center += corner;
  }
  slice_center /= static_cast<float>(slice_corners.size());

  auto max_radius = 0.0F;
  for (const auto& corner : slice_corners) {
    max_radius = (std::max)(max_radius, glm::length(corner - slice_center));
  }

  const auto safe_light_direction = ResolveSafeLightDirection(light_direction);
  auto up = glm::vec3 { 0.0F, 1.0F, 0.0F };
  if (std::abs(glm::dot(up, safe_light_direction)) > 0.98F) {
    up = glm::vec3 { 0.0F, 0.0F, 1.0F };
  }

  const auto eye
    = slice_center + safe_light_direction * (max_radius + kLightPullbackPadding);
  const auto light_view = glm::lookAtRH(eye, slice_center, up);

  auto min_bounds = glm::vec3 { (std::numeric_limits<float>::max)() };
  auto max_bounds = glm::vec3 { (std::numeric_limits<float>::lowest)() };
  for (const auto& corner : slice_corners) {
    const auto light_space = glm::vec3(light_view * glm::vec4(corner, 1.0F));
    min_bounds = glm::min(min_bounds, light_space);
    max_bounds = glm::max(max_bounds, light_space);
  }

  const auto near_plane = (std::max)(0.1F, -max_bounds.z - kShadowDepthPadding);
  const auto far_plane
    = (std::max)(near_plane + kMinCascadeSpan, -min_bounds.z + kShadowDepthPadding);
  const auto light_projection = MakeReversedZOrthographicProjectionRH_ZO(
    min_bounds.x, max_bounds.x,
    min_bounds.y, max_bounds.y, near_plane, far_plane);
  const auto width = (std::max)(max_bounds.x - min_bounds.x, kMinCascadeSpan);
  const auto height = (std::max)(max_bounds.y - min_bounds.y, kMinCascadeSpan);
  const auto resolution = (std::max)(shadow_resolution, 1U);
  return CascadeMatrixData {
    .light_view_projection = light_projection * light_view,
    .world_texel_size = (std::max)(width, height)
      / static_cast<float>(resolution),
  };
}

} // namespace

auto CascadeShadowSetup::BuildDirectionalFrameData(
  const PreparedViewShadowInput& view_input,
  const FrameDirectionalLightSelection& directional_light,
  const ConventionalShadowTargetAllocator::DirectionalAllocation& allocation) const
  -> DirectionalShadowFrameData
{
  auto frame_data = DirectionalShadowFrameData {};
  frame_data.backing_resolution = allocation.resolution;
  frame_data.storage_flags
    = allocation.surface ? kDirectionalShadowStorageDedicatedArray : 0U;

  if (!allocation.surface_srv.IsValid() || view_input.resolved_view == nullptr) {
    return frame_data;
  }

  const auto cascade_count = (std::max)(
    1U, (std::min)(directional_light.cascade_count, ShadowFrameBindings::kMaxCascades));
  const auto inverse_resolution_x = allocation.resolution.x > 0U
    ? 1.0F / static_cast<float>(allocation.resolution.x)
    : 0.0F;
  const auto inverse_resolution_y = allocation.resolution.y > 0U
    ? 1.0F / static_cast<float>(allocation.resolution.y)
    : 0.0F;

  frame_data.bindings.conventional_shadow_surface_handle = allocation.surface_srv;
  frame_data.bindings.cascade_count = cascade_count;
  frame_data.bindings.technique_flags
    = kShadowTechniqueDirectionalConventional;
  frame_data.bindings.sampling_contract_flags
    = kShadowSamplingContractTexture2DArray;

  const auto view_near = view_input.resolved_view->NearPlane();
  const auto view_far = view_input.resolved_view->FarPlane();
  const auto max_shadow_distance = std::isfinite(directional_light.max_shadow_distance)
      && directional_light.max_shadow_distance > view_near
    ? directional_light.max_shadow_distance
    : scene::kDefaultDirectionalMaxShadowDistance;
  const auto shadow_far = (std::min)(view_far, max_shadow_distance);
  const auto transition_fraction
    = std::clamp(directional_light.transition_fraction, 0.0F, 1.0F);
  const auto distance_fadeout_fraction
    = std::clamp(directional_light.distance_fadeout_fraction, 0.0F, 1.0F);

  auto cascade_begin = view_input.resolved_view->NearPlane();
  for (std::uint32_t cascade_index = 0U; cascade_index < cascade_count;
       ++cascade_index) {
    const auto cascade_end = ResolveCascadeEnd(directional_light, cascade_index,
      cascade_count, view_near, view_far);
    const auto cascade_span
      = (std::max)(cascade_end - cascade_begin, kMinCascadeSpan);
    const auto cascade_matrix = BuildCascadeMatrix(*view_input.resolved_view,
      directional_light.direction, cascade_begin, cascade_end,
      allocation.resolution.x);
    auto& cascade = frame_data.bindings.cascades[cascade_index];
    cascade.light_view_projection = cascade_matrix.light_view_projection;
    cascade.split_near = cascade_begin;
    cascade.split_far = cascade_end;
    const auto transition_width = cascade_index + 1U < cascade_count
      ? cascade_span * transition_fraction
      : 0.0F;
    const auto fade_begin = cascade_index + 1U == cascade_count
      ? cascade_end - (shadow_far - view_near) * distance_fadeout_fraction
      : shadow_far;
    cascade.sampling_metadata0 = glm::vec4(
      static_cast<float>(cascade_index), inverse_resolution_x,
      inverse_resolution_y, cascade_matrix.world_texel_size);
    cascade.sampling_metadata1 = glm::vec4(transition_width, fade_begin,
      directional_light.shadow_bias, directional_light.shadow_normal_bias);
    cascade_begin = cascade_end;
  }

  return frame_data;
}

} // namespace oxygen::vortex::shadows::internal
