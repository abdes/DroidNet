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
 Creates vertex and index buffers for a cylinder aligned along the Z axis.
 The cylinder has a side surface and two end caps at z = -height/2 and
 z = +height/2. Its radial centre is x = y = 0.

 @param segments Number of radial segments (minimum 3).
 @param height Finite positive height along Z.
 @param radius Finite positive radius in the XY plane.
 @return Vertex and index vectors, or std::nullopt for
 invalid counts/dimensions or sampled edges that collapse in float32 storage.

 ### Performance Characteristics

 - Time Complexity: O(segments).
 - Output: 4*segments+4 vertices and 12*segments indices.
 - Side and cap rim vertices are separate to preserve their normal/UV seams.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeCylinderMeshAsset(32, 1.0f, 0.5f)) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, Vertex
*/
auto oxygen::data::MakeCylinderMeshAsset(
  const unsigned int segments, const float height, const float radius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr auto kIndicesPerSegment = 12U;
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

  // Side vertices
  for (unsigned int i = 0; i <= segments; ++i) {
    const auto theta = i == segments ? 0.0 : 2.0 * pi * i / segments;
    const auto x = static_cast<float>(std::cos(theta));
    const auto y = static_cast<float>(std::sin(theta));
    if (i > 0 && x * radius == vertices.back().position.x
      && y * radius == vertices.back().position.y) {
      return std::nullopt;
    }
    glm::vec3 normal = { x, y, 0.0f };
    glm::vec3 tangent = { -y, x, 0.0f };
    glm::vec3 bitangent = { 0.0F, 0.0F, -1.0F };
    float u = static_cast<float>(i) / static_cast<float>(segments);

    // Bottom (side)
    vertices.push_back(Vertex {
      .position = { x * radius, y * radius, -half_height },
      .normal = normal,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
    // Top (side)
    vertices.push_back(Vertex {
      .position = { x * radius, y * radius, half_height },
      .normal = normal,
      .texcoord = { u, 0.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
  }

  // Side indices
  for (unsigned int i = 0; i < segments; ++i) {
    uint32_t i0 = i * 2;
    uint32_t i1 = i0 + 1;
    uint32_t i2 = i0 + 2;
    uint32_t i3 = i0 + 3;
    // CCW: (bottom_curr, bottom_next, top_curr) and (top_curr, bottom_next,
    // top_next)
    indices.push_back(i0);
    indices.push_back(i2);
    indices.push_back(i1);
    indices.push_back(i1);
    indices.push_back(i2);
    indices.push_back(i3);
  }

  // Caps
  std::vector<uint32_t> bottom_cap_rim_indices;
  std::vector<uint32_t> top_cap_rim_indices;
  for (unsigned int i = 0; i < segments; ++i) {
    const auto theta = 2.0 * pi * i / segments;
    const auto x = static_cast<float>(std::cos(theta));
    const auto y = static_cast<float>(std::sin(theta));
    float u = (x + 1.0f) * 0.5f;
    float v = (y + 1.0f) * 0.5f;

    // Bottom cap rim vertex
    vertices.push_back(Vertex {
      .position = { x * radius, y * radius, -half_height },
      .normal = { 0, 0, -1 },
      .texcoord = { u, v },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    });
    bottom_cap_rim_indices.push_back(
      static_cast<uint32_t>(vertices.size() - 1));

    // Top cap rim vertex
    vertices.push_back(Vertex {
      .position = { x * radius, y * radius, half_height },
      .normal = { 0, 0, 1 },
      .texcoord = { u, v },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    });
    top_cap_rim_indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
  }

  // Center vertices for caps
  uint32_t bottom_center_index = static_cast<uint32_t>(vertices.size());
  vertices.push_back(Vertex {
    .position = { 0, 0, -half_height },
    .normal = { 0, 0, -1 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  uint32_t top_center_index = static_cast<uint32_t>(vertices.size());
  vertices.push_back(Vertex {
    .position = { 0, 0, half_height },
    .normal = { 0, 0, 1 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });

  for (unsigned int i = 0; i < segments; ++i) {
    // Bottom cap (normal -Z): CCW is center, next, current
    indices.push_back(bottom_center_index);
    indices.push_back(bottom_cap_rim_indices[(i + 1) % segments]);
    indices.push_back(bottom_cap_rim_indices[i]);

    // Top cap (normal +Z): CCW is center, current, next
    indices.push_back(top_center_index);
    indices.push_back(top_cap_rim_indices[i]);
    indices.push_back(top_cap_rim_indices[(i + 1) % segments]);
  }

  return { { std::move(vertices), std::move(indices) } };
}
