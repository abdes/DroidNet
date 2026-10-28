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
#include <map>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

namespace {

constexpr float kSphereRadius = 0.5F;
constexpr uint32_t kMaxSubdivisionLevel = 8U;
constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] auto NormalizeSafe(const glm::vec3& v) -> glm::vec3
{
  const auto len_sq = glm::dot(v, v);
  if (len_sq <= kEpsilon) {
    return glm::vec3 { 0.0F, 0.0F, 1.0F };
  }
  return glm::normalize(v);
}

//! A triangle corner's place in the revolution layout of `space::texture`.
struct CornerLayout final {
  double u;
  float v;
  bool pole;
};

//! The layout of the unit direction `unit`: u from its azimuth (0 at `Back`,
//! counter-clockwise seen from +Z), v from its polar angle (0 at +Z).
[[nodiscard]] auto LayoutOf(const glm::vec3& unit) -> CornerLayout
{
  constexpr auto kTwoPi = 2.0 * std::numbers::pi_v<double>;
  constexpr auto kPi = std::numbers::pi_v<float>;
  const auto horizontal = std::hypot(unit.x, unit.y);
  auto u = std::atan2(-static_cast<double>(unit.x), static_cast<double>(unit.y))
    / kTwoPi;
  if (u < 0.0) {
    u += 1.0;
  }
  return {
    .u = u,
    .v = std::acos(std::clamp(unit.z, -1.0F, 1.0F)) / kPi,
    .pole = horizontal <= kEpsilon,
  };
}

//! Lays out one triangle's corners. A triangle straddling the seam continues
//! past u = 1 rather than wrapping inside it: of the ways to move corners
//! across the seam (u + 1), it takes the one with the smallest u span. A pole
//! corner takes the middle u of the triangle's other corners.
[[nodiscard]] auto LayOutTriangle(std::array<CornerLayout, 3> corners)
  -> std::array<CornerLayout, 3>
{
  auto best_span = 2.0;
  auto best_threshold = 0.0;
  for (const auto& pivot : corners) {
    if (pivot.pole) {
      continue;
    }
    // Every corner below the pivot's u moves across the seam.
    auto min_u = 2.0;
    auto max_u = -1.0;
    for (const auto& corner : corners) {
      if (!corner.pole) {
        const auto u = corner.u < pivot.u ? corner.u + 1.0 : corner.u;
        min_u = (std::min)(min_u, u);
        max_u = (std::max)(max_u, u);
      }
    }
    if (max_u - min_u < best_span) {
      best_span = max_u - min_u;
      best_threshold = pivot.u;
    }
  }
  auto min_u = 2.0;
  for (const auto& corner : corners) {
    if (!corner.pole) {
      min_u = (std::min)(min_u,
        corner.u < best_threshold ? corner.u + 1.0 : corner.u);
    }
  }
  // Keep the triangle's span starting inside [0, 1).
  const auto shift = min_u >= 1.0 ? -1.0 : 0.0;
  auto sum_u = 0.0;
  auto counted = 0;
  for (auto& corner : corners) {
    if (corner.pole) {
      continue;
    }
    corner.u += (corner.u < best_threshold ? 1.0 : 0.0) + shift;
    sum_u += corner.u;
    ++counted;
  }
  for (auto& corner : corners) {
    if (corner.pole) {
      corner.u = sum_u / counted;
    }
  }
  return corners;
}

//! The vertex at unit direction `unit` with layout `layout`.
[[nodiscard]] auto BuildVertex(
  const glm::vec3& unit, const CornerLayout& layout) -> oxygen::data::Vertex
{
  namespace procedural = oxygen::data::procedural;
  const auto ring = procedural::RingAtTurn(layout.u);
  auto sin_theta = std::hypot(unit.x, unit.y);
  auto cos_theta = unit.z;
  if (layout.pole) {
    sin_theta = 0.0F;
    cos_theta = std::copysign(1.0F, unit.z);
  }
  return oxygen::data::Vertex {
    .position = unit * kSphereRadius,
    .normal = unit,
    .texcoord = { static_cast<float>(layout.u), layout.v },
    .tangent = ring.tangent,
    .bitangent = procedural::MeridianAt(ring, sin_theta, cos_theta).bitangent,
    .color = { 1.0F, 1.0F, 1.0F, 1.0F },
  };
}

[[nodiscard]] auto EdgeKey(const uint32_t a, const uint32_t b) -> uint64_t
{
  const auto lo = (std::min)(a, b);
  const auto hi = (std::max)(a, b);
  return (static_cast<uint64_t>(lo) << 32U) | static_cast<uint64_t>(hi);
}

} // namespace

/*!
 Creates vertex and index buffers for a radius-0.5 icosphere centred at the
 origin: an icosahedron whose triangles are split `subdivision_level` times
 and pushed onto the sphere. It follows the revolution layout of
 `space::texture` (u from the azimuth with the seam at the back, v from the
 polar angle with v = 0 at +Z), so a texture reads upright and unmirrored from
 outside. Triangles straddling the seam continue past u = 1 (samplers wrap),
 and a corner at a pole takes its triangle's middle u, so the seam and the
 poles have their own vertices: weld by position to share them.

 @param subdivision_level Number of subdivisions, in [0, 8].
 @return Vertex and index vectors, or std::nullopt above the maximum level.

 ### Performance Characteristics

 - Time Complexity: O(20 * 4^subdivision_level).
 - Output: 60 * 4^subdivision_level indices.

 @see GenerateMesh, procedural::RingAtTurn
*/
auto oxygen::data::MakeIcoSphereMeshAsset(const unsigned int subdivision_level)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  if (subdivision_level > kMaxSubdivisionLevel) {
    return std::nullopt;
  }

  constexpr float phi = std::numbers::phi_v<float>;
  auto unit_positions = std::vector<glm::vec3> {
    NormalizeSafe(glm::vec3 { -1.0F, phi, 0.0F }),
    NormalizeSafe(glm::vec3 { 1.0F, phi, 0.0F }),
    NormalizeSafe(glm::vec3 { -1.0F, -phi, 0.0F }),
    NormalizeSafe(glm::vec3 { 1.0F, -phi, 0.0F }),
    NormalizeSafe(glm::vec3 { 0.0F, -1.0F, phi }),
    NormalizeSafe(glm::vec3 { 0.0F, 1.0F, phi }),
    NormalizeSafe(glm::vec3 { 0.0F, -1.0F, -phi }),
    NormalizeSafe(glm::vec3 { 0.0F, 1.0F, -phi }),
    NormalizeSafe(glm::vec3 { phi, 0.0F, -1.0F }),
    NormalizeSafe(glm::vec3 { phi, 0.0F, 1.0F }),
    NormalizeSafe(glm::vec3 { -phi, 0.0F, -1.0F }),
    NormalizeSafe(glm::vec3 { -phi, 0.0F, 1.0F }),
  };

  auto indices = std::vector<uint32_t> {
    0,
    11,
    5,
    0,
    5,
    1,
    0,
    1,
    7,
    0,
    7,
    10,
    0,
    10,
    11,
    1,
    5,
    9,
    5,
    11,
    4,
    11,
    10,
    2,
    10,
    7,
    6,
    7,
    1,
    8,
    3,
    9,
    4,
    3,
    4,
    2,
    3,
    2,
    6,
    3,
    6,
    8,
    3,
    8,
    9,
    4,
    9,
    5,
    2,
    4,
    11,
    6,
    2,
    10,
    8,
    6,
    7,
    9,
    8,
    1,
  };

  for (unsigned int level = 0; level < subdivision_level; ++level) {
    auto edge_midpoint_cache = std::unordered_map<uint64_t, uint32_t> {};
    auto refined = std::vector<uint32_t> {};
    refined.reserve(indices.size() * 4U);

    const auto midpoint_index
      = [&](const uint32_t a, const uint32_t b, auto& cache) -> uint32_t {
      const auto key = EdgeKey(a, b);
      if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
      }
      const auto midpoint
        = NormalizeSafe((unit_positions[a] + unit_positions[b]) * 0.5F);
      const auto idx = static_cast<uint32_t>(unit_positions.size());
      unit_positions.push_back(midpoint);
      cache.emplace(key, idx);
      return idx;
    };

    for (size_t i = 0; i + 2 < indices.size(); i += 3U) {
      const auto a = indices[i + 0U];
      const auto b = indices[i + 1U];
      const auto c = indices[i + 2U];

      const auto ab = midpoint_index(a, b, edge_midpoint_cache);
      const auto bc = midpoint_index(b, c, edge_midpoint_cache);
      const auto ca = midpoint_index(c, a, edge_midpoint_cache);

      refined.insert(
        refined.end(), { a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca });
    }

    indices = std::move(refined);
  }

  // Ensure final triangle winding is outward (away from origin).
  for (size_t i = 0; i + 2 < indices.size(); i += 3U) {
    const auto a = indices[i + 0U];
    const auto b = indices[i + 1U];
    const auto c = indices[i + 2U];
    const auto& pa = unit_positions[a];
    const auto& pb = unit_positions[b];
    const auto& pc = unit_positions[c];
    const auto tri_normal = glm::cross(pb - pa, pc - pa);
    const auto centroid = pa + pb + pc;
    if (glm::dot(tri_normal, centroid) < 0.0F) {
      std::swap(indices[i + 1U], indices[i + 2U]);
    }
  }

  // Split the seam and the poles: every corner takes its triangle's layout,
  // and corners with the same direction and u share one vertex.
  auto vertices = std::vector<Vertex> {};
  auto split_indices = std::vector<uint32_t> {};
  split_indices.reserve(indices.size());
  auto corner_vertices = std::map<std::pair<uint32_t, double>, uint32_t> {};
  for (size_t i = 0; i + 2 < indices.size(); i += 3U) {
    auto corners = std::array<CornerLayout, 3> {};
    for (size_t k = 0; k < corners.size(); ++k) {
      corners.at(k) = LayoutOf(unit_positions[indices[i + k]]);
    }
    corners = LayOutTriangle(corners);
    for (size_t k = 0; k < corners.size(); ++k) {
      const auto source = indices[i + k];
      const auto key = std::make_pair(source, corners.at(k).u);
      auto [it, inserted] = corner_vertices.try_emplace(
        key, static_cast<uint32_t>(vertices.size()));
      if (inserted) {
        vertices.push_back(BuildVertex(unit_positions[source], corners.at(k)));
      }
      split_indices.push_back(it->second);
    }
  }

  return { { std::move(vertices), std::move(split_indices) } };
}
