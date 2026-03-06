//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>

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

[[nodiscard]] auto BuildVertexFromUnitNormal(const glm::vec3& unit_normal)
  -> oxygen::data::Vertex
{
  const auto normal = NormalizeSafe(unit_normal);
  const auto position = normal * kSphereRadius;
  constexpr float kPi = std::numbers::pi_v<float>;
  constexpr float kTwoPi = 2.0F * kPi;

  const auto azimuth = std::atan2(normal.y, normal.x);
  const auto u = (azimuth / kTwoPi) + 0.5F;
  const auto v = 1.0F - std::acos(std::clamp(normal.z, -1.0F, 1.0F)) / kPi;

  // Derive a stable tangent basis even near poles.
  auto ref_axis = glm::vec3 { 0.0F, 0.0F, 1.0F };
  if (std::abs(normal.z) > 0.95F) {
    ref_axis = glm::vec3 { 0.0F, 1.0F, 0.0F };
  }
  auto tangent = glm::cross(ref_axis, normal);
  if (glm::dot(tangent, tangent) <= kEpsilon) {
    tangent = glm::cross(glm::vec3 { 1.0F, 0.0F, 0.0F }, normal);
  }
  tangent = NormalizeSafe(tangent);
  const auto bitangent = NormalizeSafe(glm::cross(normal, tangent));

  return oxygen::data::Vertex {
    .position = position,
    .normal = normal,
    .texcoord = { u, v },
    .tangent = tangent,
    .bitangent = bitangent,
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

auto oxygen::data::MakeIcoSphereMeshAsset(const unsigned int subdivision_level)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  if (subdivision_level > kMaxSubdivisionLevel) {
    return std::nullopt;
  }

  constexpr float phi = 1.6180339887498948482F;
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

  auto vertices = std::vector<Vertex> {};
  vertices.reserve(unit_positions.size());
  for (const auto& p : unit_positions) {
    vertices.push_back(BuildVertexFromUnitNormal(p));
  }

  return { { std::move(vertices), std::move(indices) } };
}

auto oxygen::data::MakeGeodesicSphereMeshAsset(
  const unsigned int subdivision_level)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  return MakeIcoSphereMeshAsset(subdivision_level);
}
