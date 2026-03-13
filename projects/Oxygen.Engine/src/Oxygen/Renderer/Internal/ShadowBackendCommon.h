//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>
#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Scene/Light/LightCommon.h>

namespace oxygen::renderer::internal::shadow_detail {

inline constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
inline constexpr float kDirectionalCacheFloatTolerance = 1.0e-5F;
inline constexpr std::uint32_t kDirectionalVirtualClipReuseGuardbandPages = 1U;
inline constexpr float kDirectionalVirtualDepthGuardbandSafety = 0.9F;
inline constexpr bool kDirectionalVirtualClipmapPanningEnabled = true;
inline constexpr std::uint32_t kDirectionalCoarseMarkMinClipCount = 2U;
inline constexpr std::uint32_t kDirectionalCoarseMarkMaxClipCount = 4U;
inline constexpr std::uint32_t kVirtualResidentPageCoordBits = 28U;
inline constexpr std::uint64_t kVirtualResidentPageCoordMask
  = (1ULL << kVirtualResidentPageCoordBits) - 1ULL;
inline constexpr std::uint32_t kVirtualResidentPageCoordSignBit
  = 1U << (kVirtualResidentPageCoordBits - 1U);

[[nodiscard]] inline auto HashBytes(const void* data, const std::size_t size,
  std::uint64_t hash = kFnvOffsetBasis) -> std::uint64_t
{
  const auto* bytes = static_cast<const std::byte*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(bytes[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

template <typename T>
[[nodiscard]] inline auto HashSpan(const std::span<const T> values,
  std::uint64_t hash = kFnvOffsetBasis) -> std::uint64_t
{
  const auto size = values.size();
  hash = HashBytes(&size, sizeof(size), hash);
  if (!values.empty()) {
    hash = HashBytes(values.data(), values.size_bytes(), hash);
  }
  return hash;
}

[[nodiscard]] inline auto EncodeVirtualResidentPageCoord(
  const std::int32_t value) -> std::uint64_t
{
  return static_cast<std::uint64_t>(static_cast<std::uint32_t>(value))
    & kVirtualResidentPageCoordMask;
}

[[nodiscard]] inline auto DecodeVirtualResidentPageCoord(
  const std::uint64_t encoded) -> std::int32_t
{
  auto value = static_cast<std::uint32_t>(
    encoded & kVirtualResidentPageCoordMask);
  if ((value & kVirtualResidentPageCoordSignBit) != 0U) {
    value |= static_cast<std::uint32_t>(~kVirtualResidentPageCoordMask);
  }
  return static_cast<std::int32_t>(value);
}

[[nodiscard]] inline auto PackVirtualResidentPageKey(
  const std::uint32_t clip_level, const std::int32_t grid_x,
  const std::int32_t grid_y) -> std::uint64_t
{
  return (static_cast<std::uint64_t>(clip_level) << 56ULL)
    | (EncodeVirtualResidentPageCoord(grid_x) << 28ULL)
    | EncodeVirtualResidentPageCoord(grid_y);
}

[[nodiscard]] inline auto VirtualResidentPageKeyClipLevel(
  const std::uint64_t key) -> std::uint32_t
{
  return static_cast<std::uint32_t>(key >> 56ULL);
}

[[nodiscard]] inline auto VirtualResidentPageKeyGridX(
  const std::uint64_t key) -> std::int32_t
{
  return DecodeVirtualResidentPageCoord(key >> 28ULL);
}

[[nodiscard]] inline auto VirtualResidentPageKeyGridY(
  const std::uint64_t key) -> std::int32_t
{
  return DecodeVirtualResidentPageCoord(key);
}

[[nodiscard]] inline auto CompareVirtualResidentEvictionPriority(
  const std::uint64_t lhs_key, const bool lhs_contents_valid,
  const std::uint64_t lhs_last_touched_frame, const std::uint64_t rhs_key,
  const bool rhs_contents_valid, const std::uint64_t rhs_last_touched_frame)
  -> bool
{
  if (lhs_contents_valid != rhs_contents_valid) {
    return !lhs_contents_valid && rhs_contents_valid;
  }

  const auto lhs_clip_level = VirtualResidentPageKeyClipLevel(lhs_key);
  const auto rhs_clip_level = VirtualResidentPageKeyClipLevel(rhs_key);
  if (lhs_clip_level != rhs_clip_level) {
    return lhs_clip_level > rhs_clip_level;
  }

  if (lhs_last_touched_frame != rhs_last_touched_frame) {
    return lhs_last_touched_frame < rhs_last_touched_frame;
  }

  return lhs_key < rhs_key;
}

[[nodiscard]] inline auto DirectionalCacheFloatEqual(
  const float lhs, const float rhs) -> bool
{
  return std::abs(lhs - rhs) <= kDirectionalCacheFloatTolerance;
}

[[nodiscard]] inline auto QuantizeDirectionalCacheFloat(const float value)
  -> std::int64_t
{
  constexpr double kDirectionalCacheFloatScale = 100000.0;
  return static_cast<std::int64_t>(
    std::llround(static_cast<double>(value) * kDirectionalCacheFloatScale));
}

[[nodiscard]] inline auto DirectionalCacheMat4Equal(
  const glm::mat4& lhs, const glm::mat4& rhs) -> bool
{
  for (std::uint32_t column = 0U; column < 4U; ++column) {
    for (std::uint32_t row = 0U; row < 4U; ++row) {
      if (!DirectionalCacheFloatEqual(lhs[column][row], rhs[column][row])) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] inline auto BuildDirectionalAddressSpaceComparableLightView(
  glm::mat4 light_view) -> glm::mat4
{
  // Directional page identity is defined by the clip lattice orientation and
  // page scale. The grid itself jumps by snap_size when the light eye snaps,
  // which shifts all absolute logical keys. We MUST preserve XY translation
  // so this jump correctly invalidates the cache, rather than falsely
  // accepting severely shifted feedback keys.
  // Z translation represents the light pull-back padding and changes smoothly;
  // it does not shift the XY grid, so we can safely zero it to allow reuse
  // when caster bounds expand/shrink along the light ray.
  light_view[3][2] = 0.0F;
  return light_view;
}

[[nodiscard]] inline auto BuildDirectionalCacheComparableLightView(
  glm::mat4 light_view) -> glm::mat4
{
  light_view[3][0] = 0.0F;
  light_view[3][1] = 0.0F;
  light_view[3][2] = 0.0F;
  return light_view;
}

[[nodiscard]] inline auto ResolveDirectionalVirtualClipGridCoord(
  const float clip_origin, const float page_world_size) -> std::int32_t
{
  const auto stabilized_page_world_size = std::max(
    static_cast<double>(page_world_size), static_cast<double>(1.0e-4F));
  const auto grid_coord = std::llround(
    static_cast<double>(clip_origin) / stabilized_page_world_size);
  return static_cast<std::int32_t>(grid_coord);
}

[[nodiscard]] inline auto ResolveDirectionalVirtualClipGridOriginX(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata,
  const std::uint32_t clip_index) -> std::int32_t
{
  const auto& clip = metadata.clip_metadata[clip_index];
  return ResolveDirectionalVirtualClipGridCoord(
    clip.origin_page_scale.x, clip.origin_page_scale.z);
}

[[nodiscard]] inline auto ResolveDirectionalVirtualClipGridOriginY(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata,
  const std::uint32_t clip_index) -> std::int32_t
{
  const auto& clip = metadata.clip_metadata[clip_index];
  return ResolveDirectionalVirtualClipGridCoord(
    clip.origin_page_scale.y, clip.origin_page_scale.z);
}

struct DirectionalVirtualClipmapPageOffset {
  bool valid { false };
  std::int32_t delta_x { 0 };
  std::int32_t delta_y { 0 };
};

struct DirectionalVirtualClipRelativeTransform {
  bool valid { false };
  glm::vec2 page_coord_scale { 1.0F, 1.0F };
  glm::vec2 page_coord_bias { 0.0F, 0.0F };
  float depth_scale { 1.0F };
  float depth_bias { 0.0F };
  float lod_scale { 1.0F };
};

struct DirectionalVirtualDepthRange {
  bool valid { false };
  float near_plane { 0.0F };
  float far_plane { 0.0F };
  float min_depth { 0.0F };
  float max_depth { 0.0F };
  float center_depth { 0.0F };
  float half_extent { 0.0F };
};

[[nodiscard]] inline auto ResolveDirectionalVirtualClipmapPageOffset(
  const oxygen::engine::DirectionalVirtualShadowMetadata& previous,
  const oxygen::engine::DirectionalVirtualShadowMetadata& current,
  const std::uint32_t clip_index) -> DirectionalVirtualClipmapPageOffset
{
  DirectionalVirtualClipmapPageOffset result {};
  if (clip_index >= previous.clip_level_count
    || clip_index >= current.clip_level_count) {
    return result;
  }

  const auto& previous_clip = previous.clip_metadata[clip_index];
  const auto& current_clip = current.clip_metadata[clip_index];
  if (!DirectionalCacheFloatEqual(
        previous_clip.origin_page_scale.z, current_clip.origin_page_scale.z)) {
    return result;
  }

  result.valid = true;
  result.delta_x = ResolveDirectionalVirtualClipGridOriginX(current, clip_index)
    - ResolveDirectionalVirtualClipGridOriginX(previous, clip_index);
  result.delta_y = ResolveDirectionalVirtualClipGridOriginY(current, clip_index)
    - ResolveDirectionalVirtualClipGridOriginY(previous, clip_index);
  return result;
}

[[nodiscard]] inline auto ResolveDirectionalCoarseClipCount(
  const std::uint32_t clip_level_count) -> std::uint32_t
{
  if (clip_level_count == 0U) {
    return 0U;
  }

  return std::clamp((clip_level_count + 2U) / 3U,
    std::min(kDirectionalCoarseMarkMinClipCount, clip_level_count),
    std::min(kDirectionalCoarseMarkMaxClipCount, clip_level_count));
}

[[nodiscard]] inline auto BuildDirectionalCoarseClipMask(
  const std::uint32_t clip_level_count) -> std::uint32_t
{
  if (clip_level_count == 0U) {
    return 0U;
  }

  const auto coarse_clip_count = ResolveDirectionalCoarseClipCount(
    clip_level_count);
  const auto coarse_begin = clip_level_count > coarse_clip_count
    ? clip_level_count - coarse_clip_count
    : 0U;
  std::uint32_t mask = 0U;
  for (std::uint32_t clip_index = coarse_begin; clip_index < clip_level_count;
    ++clip_index) {
    mask |= (1U << clip_index);
  }
  return mask;
}

[[nodiscard]] inline auto ResolveDirectionalCoarseBackboneBegin(
  const std::uint32_t clip_level_count) -> std::uint32_t
{
  const auto mask = BuildDirectionalCoarseClipMask(clip_level_count);
  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    if ((mask & (1U << clip_index)) != 0U) {
      return clip_index;
    }
  }
  return clip_level_count;
}

[[nodiscard]] inline auto IsDirectionalCoarseClipSelected(
  const std::uint32_t clip_mask, const std::uint32_t clip_index) -> bool
{
  return clip_index < 32U && (clip_mask & (1U << clip_index)) != 0U;
}

[[nodiscard]] inline auto BuildDirectionalVirtualClipRelativeTransform(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata,
  const std::uint32_t requested_clip_index,
  const std::uint32_t resolved_clip_index)
  -> DirectionalVirtualClipRelativeTransform
{
  DirectionalVirtualClipRelativeTransform transform {};
  if (requested_clip_index >= metadata.clip_level_count
    || resolved_clip_index >= metadata.clip_level_count) {
    return transform;
  }

  const auto& requested_clip = metadata.clip_metadata[requested_clip_index];
  const auto& resolved_clip = metadata.clip_metadata[resolved_clip_index];
  const float requested_page_world
    = std::max(requested_clip.origin_page_scale.z, 1.0e-4F);
  const float resolved_page_world
    = std::max(resolved_clip.origin_page_scale.z, 1.0e-4F);
  const float requested_depth_scale = requested_clip.origin_page_scale.w;
  const float resolved_depth_scale = resolved_clip.origin_page_scale.w;

  transform.valid = true;
  transform.page_coord_scale = glm::vec2(
    requested_page_world / resolved_page_world);
  transform.page_coord_bias = (glm::vec2(
                                 requested_clip.origin_page_scale.x,
                                 requested_clip.origin_page_scale.y)
      - glm::vec2(
        resolved_clip.origin_page_scale.x, resolved_clip.origin_page_scale.y))
    / resolved_page_world;
  if (std::abs(requested_depth_scale) > 1.0e-8F
    && std::abs(resolved_depth_scale) > 1.0e-8F) {
    transform.depth_scale = resolved_depth_scale / requested_depth_scale;
    transform.depth_bias = resolved_clip.bias_reserved.x
      - requested_clip.bias_reserved.x * transform.depth_scale;
  }
  transform.lod_scale = resolved_page_world / requested_page_world;
  return transform;
}

[[nodiscard]] inline auto TransformDirectionalRequestedPageCoordToResolvedClip(
  const glm::vec2 requested_page_coord,
  const DirectionalVirtualClipRelativeTransform& transform) -> glm::vec2
{
  return requested_page_coord * transform.page_coord_scale
    + transform.page_coord_bias;
}

[[nodiscard]] inline auto RemapDirectionalRequestedDepthToResolvedClip(
  const float requested_depth,
  const DirectionalVirtualClipRelativeTransform& transform) -> float
{
  return requested_depth * transform.depth_scale + transform.depth_bias;
}

[[nodiscard]] inline auto IsDirectionalVirtualClipReuseGuardbandValid(
  const DirectionalVirtualClipmapPageOffset& offset,
  const std::uint32_t guardband_pages) -> bool
{
  if (!offset.valid) {
    return false;
  }

  const auto guardband = static_cast<std::int32_t>(guardband_pages);
  return std::abs(offset.delta_x) <= guardband
    && std::abs(offset.delta_y) <= guardband;
}

[[nodiscard]] inline auto IsDirectionalVirtualClipmapPanningCompatible(
  const DirectionalVirtualClipmapPageOffset& offset,
  const bool panning_enabled) -> bool
{
  if (!offset.valid) {
    return false;
  }

  if (panning_enabled) {
    return true;
  }

  return offset.delta_x == 0 && offset.delta_y == 0;
}

[[nodiscard]] inline auto RecoverDirectionalVirtualDepthRange(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata)
  -> DirectionalVirtualDepthRange
{
  DirectionalVirtualDepthRange range {};
  if (metadata.clip_level_count == 0U) {
    return range;
  }

  const auto& clip = metadata.clip_metadata[0];
  const float depth_scale = clip.origin_page_scale.w;
  const float depth_bias = clip.bias_reserved.x;
  if (std::abs(depth_scale) <= 1.0e-8F) {
    return range;
  }

  range.valid = true;
  range.near_plane = depth_bias / depth_scale;
  range.far_plane = (depth_bias - 1.0F) / depth_scale;
  range.min_depth = -range.far_plane;
  range.max_depth = -range.near_plane;
  range.center_depth = 0.5F * (range.min_depth + range.max_depth);
  range.half_extent = 0.5F * (range.max_depth - range.min_depth);
  return range;
}

[[nodiscard]] inline auto IsDirectionalVirtualDepthGuardbandValid(
  const oxygen::engine::DirectionalVirtualShadowMetadata& previous,
  const float current_required_min_depth,
  const float current_required_max_depth,
  const float safety_ratio = kDirectionalVirtualDepthGuardbandSafety) -> bool
{
  const auto cached_range = RecoverDirectionalVirtualDepthRange(previous);
  if (!cached_range.valid) {
    return false;
  }

  const float guarded_half_extent = cached_range.half_extent * safety_ratio;
  const float guarded_min = cached_range.center_depth - guarded_half_extent;
  const float guarded_max = cached_range.center_depth + guarded_half_extent;
  return current_required_min_depth >= guarded_min
    && current_required_max_depth <= guarded_max;
}

[[nodiscard]] inline auto IsDirectionalVirtualCacheLayoutCompatible(
  const oxygen::engine::DirectionalVirtualShadowMetadata& previous,
  const oxygen::engine::DirectionalVirtualShadowMetadata& current) -> bool
{
  if (previous.clip_level_count != current.clip_level_count
    || previous.pages_per_axis != current.pages_per_axis
    || previous.page_size_texels != current.page_size_texels) {
    return false;
  }

  if (!DirectionalCacheMat4Equal(
        BuildDirectionalCacheComparableLightView(previous.light_view),
        BuildDirectionalCacheComparableLightView(current.light_view))) {
    return false;
  }

  for (std::uint32_t clip_index = 0U; clip_index < current.clip_level_count;
    ++clip_index) {
    if (!DirectionalCacheFloatEqual(
          previous.clip_metadata[clip_index].origin_page_scale.z,
          current.clip_metadata[clip_index].origin_page_scale.z)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] inline auto HashDirectionalVirtualFeedbackAddressSpace(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata)
  -> std::uint64_t
{
  std::uint64_t hash = HashBytes(
    &metadata.clip_level_count, sizeof(metadata.clip_level_count));
  hash = HashBytes(&metadata.pages_per_axis, sizeof(metadata.pages_per_axis),
    hash);
  const auto lattice_light_view
    = BuildDirectionalAddressSpaceComparableLightView(metadata.light_view);
  for (std::uint32_t column = 0U; column < 4U; ++column) {
    for (std::uint32_t row = 0U; row < 4U; ++row) {
      const auto quantized
        = QuantizeDirectionalCacheFloat(lattice_light_view[column][row]);
      hash = HashBytes(&quantized, sizeof(quantized), hash);
    }
  }

  const auto clip_level_count = std::min(metadata.clip_level_count,
    oxygen::engine::kMaxVirtualDirectionalClipLevels);
  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    const auto& clip = metadata.clip_metadata[clip_index];
    const auto page_world_size
      = QuantizeDirectionalCacheFloat(clip.origin_page_scale.z);
    hash = HashBytes(&page_world_size, sizeof(page_world_size), hash);
  }
  return hash;
}

[[nodiscard]] inline auto HashDirectionalVirtualFeedbackLattice(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata)
  -> std::uint64_t
{
  return HashDirectionalVirtualFeedbackAddressSpace(metadata);
}

[[nodiscard]] inline auto BuildShadowProductFlags(
  const std::uint32_t light_flags) -> std::uint32_t
{
  using oxygen::engine::DirectionalLightFlags;
  using oxygen::engine::ShadowProductFlags;

  auto flags = ShadowProductFlags::kValid;
  const auto directional_flags
    = static_cast<DirectionalLightFlags>(light_flags);

  if ((directional_flags & DirectionalLightFlags::kContactShadows)
    != DirectionalLightFlags::kNone) {
    flags |= ShadowProductFlags::kContactShadows;
  }
  if ((directional_flags & DirectionalLightFlags::kSunLight)
    != DirectionalLightFlags::kNone) {
    flags |= ShadowProductFlags::kSunLight;
  }

  return static_cast<std::uint32_t>(flags);
}

[[nodiscard]] inline auto NormalizeOrFallback(
  const glm::vec3& value, const glm::vec3& fallback) -> glm::vec3
{
  const float len_sq = glm::dot(value, value);
  if (len_sq <= oxygen::math::EpsilonDirection) {
    return fallback;
  }
  return glm::normalize(value);
}

[[nodiscard]] inline auto ShadowResolutionFromHint(const std::uint32_t hint)
  -> std::uint32_t
{
  using oxygen::scene::ShadowResolutionHint;
  switch (static_cast<ShadowResolutionHint>(hint)) {
  case ShadowResolutionHint::kLow:
    return 1024U;
  case ShadowResolutionHint::kMedium:
    return 2048U;
  case ShadowResolutionHint::kHigh:
    return 3072U;
  case ShadowResolutionHint::kUltra:
    return 4096U;
  default:
    return 2048U;
  }
}

[[nodiscard]] inline auto ApplyDirectionalShadowQualityTier(
  const std::uint32_t authored_resolution,
  const oxygen::ShadowQualityTier quality_tier,
  const std::size_t directional_candidate_count) -> std::uint32_t
{
  const bool single_dominant_directional = directional_candidate_count <= 1U;
  std::uint32_t preferred_min_resolution = authored_resolution;
  std::uint32_t max_resolution = authored_resolution;

  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    preferred_min_resolution = 1024U;
    max_resolution = 1024U;
    break;
  case oxygen::ShadowQualityTier::kMedium:
    preferred_min_resolution = 2048U;
    max_resolution = 2048U;
    break;
  case oxygen::ShadowQualityTier::kHigh:
    preferred_min_resolution = single_dominant_directional ? 3072U : 2048U;
    max_resolution = 3072U;
    break;
  case oxygen::ShadowQualityTier::kUltra:
    preferred_min_resolution = single_dominant_directional ? 4096U : 3072U;
    max_resolution = 4096U;
    break;
  default:
    preferred_min_resolution = 2048U;
    max_resolution = 2048U;
    break;
  }

  return std::clamp(std::max(authored_resolution, preferred_min_resolution),
    1024U, max_resolution);
}

[[nodiscard]] inline auto TransformPoint(
  const glm::mat4& matrix, const glm::vec3& point) -> glm::vec3
{
  const glm::vec4 transformed = matrix * glm::vec4(point, 1.0F);
  const float inv_w
    = std::abs(transformed.w) > 1.0e-6F ? (1.0F / transformed.w) : 1.0F;
  return glm::vec3(transformed) * inv_w;
}

[[nodiscard]] inline auto BuildDirectionalLightRotation(
  const glm::vec3& light_dir_to_surface, const glm::vec3& up) -> glm::mat4
{
  return glm::lookAtRH(glm::vec3(0.0F), light_dir_to_surface, up);
}

inline auto TightenDepthRangeWithShadowCasters(
  const std::span<const glm::vec4> shadow_caster_bounds,
  const glm::mat4& light_view, const float ortho_half_extent_x,
  const float ortho_half_extent_y, float& min_depth, float& max_depth) -> bool
{
  bool tightened = false;
  for (const auto& sphere : shadow_caster_bounds) {
    const float radius = sphere.w;
    if (radius <= 0.0F) {
      continue;
    }

    const glm::vec3 center_ls
      = glm::vec3(light_view * glm::vec4(glm::vec3(sphere), 1.0F));
    if (std::abs(center_ls.x) > ortho_half_extent_x + radius
      || std::abs(center_ls.y) > ortho_half_extent_y + radius) {
      continue;
    }

    min_depth = std::min(min_depth, center_ls.z - radius);
    max_depth = std::max(max_depth, center_ls.z + radius);
    tightened = true;
  }

  return tightened;
}

} // namespace oxygen::renderer::internal::shadow_detail
