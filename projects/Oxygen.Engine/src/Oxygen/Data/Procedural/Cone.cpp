//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

/*!
 Creates vertex and index buffers for a cone aligned along the Z axis.
 The base cap lies at z = -height/2 and the apex at z = +height/2, with the
 axis passing through x = y = 0. The origin is halfway along the height,
 not the cone's centre of mass.

 @param segments Number of radial segments (minimum 3).
 @param height Finite positive height along Z.
 @param radius Finite positive base radius in the XY plane.
 @return Vertex and index vectors, or std::nullopt for
 invalid counts/dimensions or sampled edges that collapse in float32 storage.

 ### Performance Characteristics

 - Time Complexity: O(segments).
 - Output: 2*segments+3 vertices and 6*segments indices.
 - Side and cap rim vertices are separate to preserve their normal/UV seams.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeConeMeshAsset(32, 1.0f, 0.5f)) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, Vertex
*/
auto oxygen::data::MakeConeMeshAsset(
  unsigned int segments, float height, float radius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr auto kIndicesPerSegment = 6U;
  const auto half_height = height / 2.0F;
  if (segments < 3
    || segments > std::numeric_limits<uint32_t>::max() / kIndicesPerSegment
    || !std::isfinite(height) || !std::isfinite(radius) || half_height <= 0.0F
    || radius <= 0.0F) {
    return std::nullopt;
  }
  constexpr double pi = std::numbers::pi_v<double>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  glm::vec3 apex_pos = { 0.0f, 0.0f, half_height };
  const auto slant = std::hypot(static_cast<double>(height), radius);
  const auto normal_radius = static_cast<float>(height / slant);
  const auto normal_height = static_cast<float>(radius / slant);

  // Side vertices (base ring)
  for (unsigned int i = 0; i <= segments; ++i) {
    const auto theta = i == segments ? 0.0 : 2.0 * pi * i / segments;
    const auto x = static_cast<float>(std::cos(theta));
    const auto y = static_cast<float>(std::sin(theta));
    float u = static_cast<float>(i) / static_cast<float>(segments);
    glm::vec3 pos = { x * radius, y * radius, -half_height };
    if (i > 0 && pos == vertices.back().position) {
      return std::nullopt;
    }
    // Normal points out and up
    glm::vec3 dir = { x * normal_radius, y * normal_radius, normal_height };
    glm::vec3 tangent = { -y, x, 0.0f };
    glm::vec3 bitangent = -glm::cross(dir, tangent);
    vertices.push_back(Vertex {
      .position = pos,
      .normal = dir,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
  }

  // Apex vertex
  vertices.push_back(Vertex {
    .position = apex_pos,
    .normal = { 0.0f, 0.0f, 1.0f },
    .texcoord = { 0.5f, 0.0f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  uint32_t apex_index = static_cast<uint32_t>(vertices.size() - 1);

  // Side indices: CCW is (current, next, apex)
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(i);
    indices.push_back(i + 1);
    indices.push_back(apex_index);
  }

  // Base cap rim vertices
  std::vector<uint32_t> base_cap_rim_indices;
  for (unsigned int i = 0; i < segments; ++i) {
    const auto theta = 2.0 * pi * i / segments;
    const auto x = static_cast<float>(std::cos(theta));
    const auto y = static_cast<float>(std::sin(theta));
    float u = (x + 1.0f) * 0.5f;
    float v = (y + 1.0f) * 0.5f;
    vertices.push_back(Vertex {
      .position = { x * radius, y * radius, -half_height },
      .normal = { 0, 0, -1 },
      .texcoord = { u, v },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    });
    base_cap_rim_indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
  }

  // Center vertex for base cap
  vertices.push_back(Vertex {
    .position = { 0, 0, -half_height },
    .normal = { 0, 0, -1 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  uint32_t base_center = static_cast<uint32_t>(vertices.size() - 1);

  // Base cap indices: CCW is (center, next, current)
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(base_center);
    indices.push_back(base_cap_rim_indices[(i + 1) % segments]);
    indices.push_back(base_cap_rim_indices[i]);
  }

  return { { std::move(vertices), std::move(indices) } };
}
