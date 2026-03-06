//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <unordered_map>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Data/ProceduralMeshes.h>

namespace {

struct GridKey final {
  uint16_t x { 0U };
  uint16_t y { 0U };
  uint16_t z { 0U };

  [[nodiscard]] auto operator==(const GridKey&) const noexcept -> bool
    = default;
};

struct GridKeyHash final {
  [[nodiscard]] auto operator()(const GridKey& key) const noexcept -> size_t
  {
    size_t seed = std::hash<uint16_t> {}(key.x);
    seed ^= std::hash<uint16_t> {}(key.y) + 0x9e3779b9U + (seed << 6U)
      + (seed >> 2U);
    seed ^= std::hash<uint16_t> {}(key.z) + 0x9e3779b9U + (seed << 6U)
      + (seed >> 2U);
    return seed;
  }
};

[[nodiscard]] auto SafeNormalize(
  const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3
{
  const auto length = glm::length(value);
  if (length > 1.0e-6F) {
    return value / length;
  }
  return fallback;
}

[[nodiscard]] auto BuildVertex(const glm::vec3& position)
  -> oxygen::data::Vertex
{
  auto normal = SafeNormalize(position, glm::vec3(0.0F, 0.0F, 1.0F));
  auto tangent = SafeNormalize(
    glm::vec3(-normal.z, 0.0F, normal.x), glm::vec3(1.0F, 0.0F, 0.0F));
  auto bitangent
    = SafeNormalize(glm::cross(normal, tangent), glm::vec3(0.0F, 1.0F, 0.0F));

  const auto u = std::atan2(position.z, position.x)
    / (2.0F * std::numbers::pi_v<float>)+0.5F;
  const auto v = std::clamp(position.y + 0.5F, 0.0F, 1.0F);

  return {
    .position = position,
    .normal = normal,
    .texcoord = glm::vec2(u, v),
    .tangent = tangent,
    .bitangent = bitangent,
    .color = glm::vec4(1.0F),
  };
}

auto AddOrGetVertex(const uint32_t gx, const uint32_t gy, const uint32_t gz,
  const uint32_t segments,
  std::unordered_map<GridKey, uint32_t, GridKeyHash>& vertex_lut,
  std::vector<oxygen::data::Vertex>& vertices) -> uint32_t
{
  const auto key = GridKey {
    .x = static_cast<uint16_t>(gx),
    .y = static_cast<uint16_t>(gy),
    .z = static_cast<uint16_t>(gz),
  };

  const auto it = vertex_lut.find(key);
  if (it != vertex_lut.end()) {
    return it->second;
  }

  const auto scale = 1.0F / static_cast<float>(segments);
  const auto position = glm::vec3(-0.5F + static_cast<float>(gx) * scale,
    -0.5F + static_cast<float>(gy) * scale,
    -0.5F + static_cast<float>(gz) * scale);

  const auto index = static_cast<uint32_t>(vertices.size());
  vertices.push_back(BuildVertex(position));
  vertex_lut.insert_or_assign(key, index);
  return index;
}

auto AddOrientedTriangle(const uint32_t a, uint32_t b, uint32_t c,
  const glm::vec3& outward_normal, const std::vector<oxygen::data::Vertex>& vtx,
  std::vector<uint32_t>& indices) -> void
{
  const auto& pa = vtx[a].position;
  const auto& pb = vtx[b].position;
  const auto& pc = vtx[c].position;
  const auto tri_normal = glm::cross(pb - pa, pc - pa);
  if (glm::dot(tri_normal, outward_normal) < 0.0F) {
    std::swap(b, c);
  }

  indices.push_back(a);
  indices.push_back(b);
  indices.push_back(c);
}

template <typename CoordFn>
auto EmitFace(const uint32_t segments, const glm::vec3& outward_normal,
  CoordFn coord_fn, std::unordered_map<GridKey, uint32_t, GridKeyHash>& lut,
  std::vector<oxygen::data::Vertex>& vertices, std::vector<uint32_t>& indices)
  -> void
{
  for (uint32_t v = 0; v < segments; ++v) {
    for (uint32_t u = 0; u < segments; ++u) {
      const auto p00 = coord_fn(u, v);
      const auto p10 = coord_fn(u + 1U, v);
      const auto p11 = coord_fn(u + 1U, v + 1U);
      const auto p01 = coord_fn(u, v + 1U);

      const auto i00
        = AddOrGetVertex(p00.x, p00.y, p00.z, segments, lut, vertices);
      const auto i10
        = AddOrGetVertex(p10.x, p10.y, p10.z, segments, lut, vertices);
      const auto i11
        = AddOrGetVertex(p11.x, p11.y, p11.z, segments, lut, vertices);
      const auto i01
        = AddOrGetVertex(p01.x, p01.y, p01.z, segments, lut, vertices);

      AddOrientedTriangle(i00, i10, i11, outward_normal, vertices, indices);
      AddOrientedTriangle(i00, i11, i01, outward_normal, vertices, indices);
    }
  }
}

} // namespace

auto oxygen::data::MakeSubdividedCubeMeshAsset(const unsigned int segments)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr unsigned int kMaxSegments = 64U;
  if (segments == 0U || segments > kMaxSegments) {
    return std::nullopt;
  }

  auto vertices = std::vector<Vertex> {};
  auto indices = std::vector<uint32_t> {};
  auto vertex_lut = std::unordered_map<GridKey, uint32_t, GridKeyHash> {};

  const auto quads_per_face = segments * segments;
  vertices.reserve(6U * quads_per_face + 2U);
  indices.reserve(6U * quads_per_face * 6U);
  vertex_lut.reserve(vertices.capacity());

  const auto s = static_cast<uint32_t>(segments);

  EmitFace(
    s, glm::vec3(1.0F, 0.0F, 0.0F),
    [s](const uint32_t u, const uint32_t v) { return glm::uvec3(s, v, u); },
    vertex_lut, vertices, indices);

  EmitFace(
    s, glm::vec3(-1.0F, 0.0F, 0.0F),
    [](const uint32_t u, const uint32_t v) { return glm::uvec3(0U, v, u); },
    vertex_lut, vertices, indices);

  EmitFace(
    s, glm::vec3(0.0F, 1.0F, 0.0F),
    [s](const uint32_t u, const uint32_t v) { return glm::uvec3(u, s, v); },
    vertex_lut, vertices, indices);

  EmitFace(
    s, glm::vec3(0.0F, -1.0F, 0.0F),
    [](const uint32_t u, const uint32_t v) { return glm::uvec3(u, 0U, v); },
    vertex_lut, vertices, indices);

  EmitFace(
    s, glm::vec3(0.0F, 0.0F, 1.0F),
    [s](const uint32_t u, const uint32_t v) { return glm::uvec3(u, v, s); },
    vertex_lut, vertices, indices);

  EmitFace(
    s, glm::vec3(0.0F, 0.0F, -1.0F),
    [](const uint32_t u, const uint32_t v) { return glm::uvec3(u, v, 0U); },
    vertex_lut, vertices, indices);

  return std::make_pair(std::move(vertices), std::move(indices));
}
