//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <numbers>
#include <string_view>
#include <vector>

/*!
 Creates a new Mesh representing a cylinder centered at the origin, aligned
 along the Y axis. The cylinder consists of a side surface and two end caps.
 Vertices are generated with positions, normals, texcoords, tangents,
 bitangents, and color.

 @param segments Number of radial segments (minimum 3).
 @param height Height of the cylinder (centered at Y=0).
 @param radius Radius of the cylinder.
 @return Shared pointer to the immutable Mesh containing the cylinder
 geometry. Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(segments)
 - Memory: Allocates space for 2*(segments+1) + 2 vertices and 12*segments
 indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a cylinder mesh asset
auto cylinder = MakeCylinderMeshAsset(32, 1.0f, 0.5f);
for (const auto& v : cylinder->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
 using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto oxygen::data::MakeCylinderMeshAsset(
  const unsigned int segments, const float height, const float radius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  if (segments < 3 || height <= 0.0f || radius <= 0.0f) {
    return std::nullopt;
  }
  constexpr float pi = std::numbers::pi_v<float>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  float half_height = height * 0.5f;
  // Side vertices
  for (unsigned int i = 0; i <= segments; ++i) {
    float theta = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    glm::vec3 normal = { x, 0.0f, z };
    glm::vec3 tangent = { -z, 0.0f, x };
    glm::vec3 bitangent = { 0.0f, 1.0f, 0.0f };
    float u = static_cast<float>(i) / static_cast<float>(segments);
    // Bottom (side)
    vertices.push_back(Vertex {
      .position = { x * radius, -half_height, z * radius },
      .normal = normal,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
    // Top (side)
    vertices.push_back(Vertex {
      .position = { x * radius, half_height, z * radius },
      .normal = normal,
      .texcoord = { u, 0.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
  }

  // --- Fix: Duplicate rim vertices for caps with correct normals ---
  // This ensures cap triangles use only vertices with up/down normals,
  // so shading is correct and not interpolated with side normals.
  std::vector<uint32_t> bottom_cap_rim_indices;
  std::vector<uint32_t> top_cap_rim_indices;
  for (unsigned int i = 0; i < segments; ++i) {
    float theta = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    float u = (x + 1.0f) * 0.5f;
    float v = (z + 1.0f) * 0.5f;
    // Bottom cap rim vertex
    vertices.push_back(Vertex {
      .position = { x * radius, -half_height, z * radius },
      .normal = { 0, -1, 0 }, // Downward normal for bottom cap
      .texcoord = { u, v },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 0, 1 },
      .color = { 1, 1, 1, 1 },
    });
    bottom_cap_rim_indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
    // Top cap rim vertex
    vertices.push_back(Vertex {
      .position = { x * radius, half_height, z * radius },
      .normal = { 0, 1, 0 }, // Upward normal for top cap
      .texcoord = { u, v },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 0, 1 },
      .color = { 1, 1, 1, 1 },
    });
    top_cap_rim_indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
  }
  // Side indices
  for (unsigned int i = 0; i < segments; ++i) {
    uint32_t i0 = i * 2;
    uint32_t i1 = i0 + 1;
    uint32_t i2 = i0 + 2;
    uint32_t i3 = i0 + 3;
    // Ensure side triangles are wound CCW when viewed from outside so
    // the computed normals point outward. Use (bottom_current, top_current,
    // bottom_next) and (top_current, top_next, bottom_next).
    indices.push_back(i0);
    indices.push_back(i1);
    indices.push_back(i2);
    indices.push_back(i1);
    indices.push_back(i3);
    indices.push_back(i2);
  }
  // Center vertices for caps
  uint32_t bottom_center_index = static_cast<uint32_t>(vertices.size());
  vertices.push_back(Vertex {
    .position = { 0, -half_height, 0 },
    .normal = { 0, -1, 0 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = { 1, 1, 1, 1 },
  }); // bottom center
  uint32_t top_center_index = static_cast<uint32_t>(vertices.size());
  vertices.push_back(Vertex {
    .position = { 0, half_height, 0 },
    .normal = { 0, 1, 0 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = { 1, 1, 1, 1 },
  }); // top center

  // --- Fix: Use duplicated rim vertices for caps ---
  // This ensures all cap triangles use only vertices with correct up/down normals.
  for (unsigned int i = 0; i < segments; ++i) {
    // Bottom cap
    uint32_t v0 = bottom_cap_rim_indices[i];
    uint32_t v1 = bottom_cap_rim_indices[(i + 1) % segments];
    // Bottom cap: wind so normal points down (-Y) -> use center, current, next
    indices.push_back(bottom_center_index);
    indices.push_back(v0);
    indices.push_back(v1);
    // Top cap
    uint32_t v2 = top_cap_rim_indices[i];
    uint32_t v3 = top_cap_rim_indices[(i + 1) % segments];
    // Top cap: wind so normal points up (+Y) -> use center, next, current
    indices.push_back(top_center_index);
    indices.push_back(v3);
    indices.push_back(v2);
  }

  return { { std::move(vertices), std::move(indices) } };
}
