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
#include <glm/matrix.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace oxygen::vortex::shadows::internal {

namespace {

constexpr float kMinCascadeSpan = 0.1F;
constexpr float kDirectionalDepthRangeClamp = 5000.0F;
constexpr float kShadowSnapDownsampleFactor = 4.0F;
constexpr float kUeCsmShadowDepthBias = 10.0F;
constexpr float kUeCascadeBiasDistribution = 1.0F;
constexpr float kUeMaxUserShadowBias = 10.0F;

struct CascadeMatrixData {
  glm::mat4 light_view_projection { 1.0F };
  float world_texel_size { 0.0F };
  float cascade_radius { 1.0F };
  float depth_span { 1.0F };
};

struct LightBasis {
  glm::vec3 right { 1.0F, 0.0F, 0.0F };
  glm::vec3 up { 0.0F, 1.0F, 0.0F };
  glm::vec3 forward { 0.0F, 0.0F, 1.0F };
};

struct CascadeSphere {
  glm::vec3 center { 0.0F };
  float radius { 1.0F };
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
  const auto near_ndc_z = resolved_view.ReverseZ() ? 1.0F : 0.0F;
  const auto far_ndc_z = resolved_view.ReverseZ() ? 0.0F : 1.0F;
  const std::array<glm::vec3, 8> kNdcCorners {
    glm::vec3 { -1.0F, -1.0F, near_ndc_z },
    glm::vec3 { 1.0F, -1.0F, near_ndc_z },
    glm::vec3 { 1.0F, 1.0F, near_ndc_z },
    glm::vec3 { -1.0F, 1.0F, near_ndc_z },
    glm::vec3 { -1.0F, -1.0F, far_ndc_z },
    glm::vec3 { 1.0F, -1.0F, far_ndc_z },
    glm::vec3 { 1.0F, 1.0F, far_ndc_z },
    glm::vec3 { -1.0F, 1.0F, far_ndc_z },
  };

  auto corners = std::array<glm::vec3, 8> {};
  const auto inverse_view_projection = glm::inverse(
    resolved_view.StableProjectionMatrix() * resolved_view.ViewMatrix());
  for (std::size_t i = 0; i < kNdcCorners.size(); ++i) {
    const auto clip
      = inverse_view_projection * glm::vec4(kNdcCorners[i], 1.0F);
    corners[i] = glm::vec3(clip) / clip.w;
  }
  return corners;
}

auto BuildLightBasis(const glm::vec3 light_direction_to_source) -> LightBasis
{
  const auto forward = ResolveSafeLightDirection(light_direction_to_source);
  auto world_up = glm::vec3 { 0.0F, 1.0F, 0.0F };
  if (std::abs(glm::dot(world_up, forward)) > 0.98F) {
    world_up = glm::vec3 { 0.0F, 0.0F, 1.0F };
  }

  const auto view_forward = -forward;
  const auto right = glm::normalize(glm::cross(view_forward, world_up));
  const auto up = glm::cross(right, view_forward);
  return LightBasis {
    .right = right,
    .up = up,
    .forward = forward,
  };
}

auto ProjectToLightBasis(const LightBasis& basis, const glm::vec3 world_position)
  -> glm::vec3
{
  return glm::vec3 {
    glm::dot(basis.right, world_position),
    glm::dot(basis.up, world_position),
    glm::dot(basis.forward, world_position),
  };
}

auto UnprojectFromLightBasis(
  const LightBasis& basis, const glm::vec3 light_position) -> glm::vec3
{
  return basis.right * light_position.x + basis.up * light_position.y
    + basis.forward * light_position.z;
}

auto SnapCascadeCenterToTexelGrid(const LightBasis& basis,
  const glm::vec3 cascade_center, const float cascade_radius,
  const std::uint32_t shadow_resolution) -> glm::vec3
{
  const auto resolution = (std::max)(shadow_resolution, 1U);
  const auto world_texel_size
    = (2.0F * cascade_radius) / static_cast<float>(resolution);
  const auto snap_interval = world_texel_size * kShadowSnapDownsampleFactor;
  if (!std::isfinite(snap_interval) || snap_interval <= 0.0F) {
    return cascade_center;
  }

  auto light_center = ProjectToLightBasis(basis, cascade_center);
  light_center.x -= std::fmod(light_center.x, snap_interval);
  light_center.y -= std::fmod(light_center.y, snap_interval);
  return UnprojectFromLightBasis(basis, light_center);
}

auto ComputeStableCascadeSphere(const std::array<glm::vec3, 8>& corners)
  -> CascadeSphere
{
  auto near_center = glm::vec3 { 0.0F };
  auto far_center = glm::vec3 { 0.0F };
  for (std::size_t i = 0; i < 4U; ++i) {
    near_center += corners[i];
    far_center += corners[i + 4U];
  }
  near_center *= 0.25F;
  far_center *= 0.25F;

  const auto split_axis = far_center - near_center;
  const auto split_length = glm::length(split_axis);
  if (!std::isfinite(split_length) || split_length <= kMinCascadeSpan) {
    return CascadeSphere {
      .center = near_center,
      .radius = 1.0F,
    };
  }

  const auto camera_direction = split_axis / split_length;
  auto near_diagonal_sq = 0.0F;
  auto far_diagonal_sq = 0.0F;
  for (std::size_t i = 0; i < 4U; ++i) {
    const auto near_offset = corners[i] - near_center;
    const auto far_offset = corners[i + 4U] - far_center;
    near_diagonal_sq = (std::max)(
      near_diagonal_sq, glm::dot(near_offset, near_offset));
    far_diagonal_sq = (std::max)(
      far_diagonal_sq, glm::dot(far_offset, far_offset));
  }

  const auto optimal_offset = ((near_diagonal_sq - far_diagonal_sq)
                                / (2.0F * split_length))
    + split_length * 0.5F;
  const auto center_distance = std::clamp(
    split_length - optimal_offset, 0.0F, split_length);
  const auto sphere_center = near_center + camera_direction * center_distance;

  auto radius_sq = 0.0F;
  for (const auto& corner : corners) {
    const auto corner_offset = corner - sphere_center;
    radius_sq = (std::max)(
      radius_sq, glm::dot(corner_offset, corner_offset));
  }

  return CascadeSphere {
    .center = sphere_center,
    .radius = (std::max)(std::sqrt(radius_sq), 1.0F),
  };
}

auto ComputeDirectionalCsmDepthBias(const float user_shadow_bias,
  const float cascade_radius, const float projection_depth_span,
  const std::uint32_t shadow_resolution) -> float
{
  if (!std::isfinite(user_shadow_bias) || user_shadow_bias <= 0.0F) {
    return 0.0F;
  }

  const auto safe_depth_span
    = (std::max)(projection_depth_span, kMinCascadeSpan);
  const auto resolution = (std::max)(shadow_resolution, 1U);
  const auto world_space_texel_scale
    = cascade_radius / static_cast<float>(resolution);
  const auto base_depth_bias = kUeCsmShadowDepthBias / safe_depth_span;
  const auto scaled_depth_bias = base_depth_bias * world_space_texel_scale;
  const auto distributed_depth_bias = base_depth_bias
    + (scaled_depth_bias - base_depth_bias) * kUeCascadeBiasDistribution;
  const auto clamped_user_bias
    = std::clamp(user_shadow_bias, 0.0F, kUeMaxUserShadowBias);
  return (std::max)(distributed_depth_bias * clamped_user_bias, 0.0F);
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

  const auto cascade_sphere = ComputeStableCascadeSphere(slice_corners);
  const auto cascade_radius = cascade_sphere.radius;
  const auto light_basis = BuildLightBasis(light_direction);
  const auto snapped_center = SnapCascadeCenterToTexelGrid(
    light_basis, cascade_sphere.center, cascade_radius, shadow_resolution);
  const auto depth_extent = (std::max)({
    kDirectionalDepthRangeClamp,
    cascade_radius,
    split_far,
  });
  const auto eye = snapped_center + light_basis.forward * depth_extent;
  const auto light_view = glm::lookAtRH(eye, snapped_center, light_basis.up);
  const auto light_projection = MakeReversedZOrthographicProjectionRH_ZO(
    -cascade_radius, cascade_radius,
    -cascade_radius, cascade_radius, 0.1F, 2.0F * depth_extent);
  const auto resolution = (std::max)(shadow_resolution, 1U);
  return CascadeMatrixData {
    .light_view_projection = light_projection * light_view,
    .world_texel_size = (2.0F * cascade_radius) / static_cast<float>(resolution),
    .cascade_radius = cascade_radius,
    .depth_span = 2.0F * depth_extent,
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
  frame_data.bindings.light_direction_to_source
    = glm::vec4(ResolveSafeLightDirection(directional_light.direction), 0.0F);

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
    const auto transition_width = cascade_index + 1U < cascade_count
      ? cascade_span * transition_fraction
      : 0.0F;
    const auto projection_end = cascade_index + 1U < cascade_count
      ? cascade_end + transition_width
      : cascade_end;
    const auto cascade_matrix = BuildCascadeMatrix(*view_input.resolved_view,
      directional_light.direction, cascade_begin, projection_end,
      allocation.resolution.x);
    auto& cascade = frame_data.bindings.cascades[cascade_index];
    cascade.light_view_projection = cascade_matrix.light_view_projection;
    cascade.split_near = cascade_begin;
    cascade.split_far = projection_end;
    const auto fade_begin = cascade_index + 1U == cascade_count
      ? cascade_end - (shadow_far - view_near) * distance_fadeout_fraction
      : shadow_far;
    cascade.sampling_metadata0 = glm::vec4(
      static_cast<float>(cascade_index), inverse_resolution_x,
      inverse_resolution_y, cascade_matrix.world_texel_size);
    const auto depth_bias = ComputeDirectionalCsmDepthBias(
      directional_light.shadow_bias, cascade_matrix.cascade_radius,
      cascade_matrix.depth_span, allocation.resolution.x);
    cascade.sampling_metadata1 = glm::vec4(transition_width, fade_begin,
      depth_bias, directional_light.shadow_normal_bias);
    cascade_begin = cascade_end;
  }

  return frame_data;
}

} // namespace oxygen::vortex::shadows::internal
