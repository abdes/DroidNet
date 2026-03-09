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
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Scene/Light/LightCommon.h>

namespace oxygen::renderer::internal::shadow_detail {

inline constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

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
