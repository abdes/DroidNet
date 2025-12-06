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
 Creates a new Mesh representing a cone centered at the origin, aligned
 along the Y axis. The cone consists of a side surface and a base cap.
 Vertices are generated with positions, normals, texcoords, tangents,
 bitangents, and color, using designated initializers and trailing commas.

 @param segments Number of radial segments (minimum 3).
 @param height Height of the cone (centered at Y=0, apex at +Y).
 @param radius Base radius of the cone.
 @return Shared pointer to the immutable Mesh containing the cone geometry.
 Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(segments)
 - Memory: Allocates space for (segments+2) vertices and 6*segments indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a cone mesh asset
auto cone = MakeConeMeshAsset(32, 1.0f, 0.5f);
for (const auto& v : cone->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
 using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto oxygen::data::MakeConeMeshAsset(
  unsigned int segments, float height, float radius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  if (segments < 3 || height <= 0.0f || radius <= 0.0f) {
    return std::nullopt;
  }
  constexpr float pi = std::numbers::pi_v<float>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  float half_height = height * 0.5f;
  glm::vec3 apex = { 0.0f, half_height, 0.0f };
  // Side vertices (base ring)
  for (unsigned int i = 0; i <= segments; ++i) {
    float theta = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    float u = static_cast<float>(i) / static_cast<float>(segments);
    glm::vec3 pos = { x * radius, -half_height, z * radius };
    glm::vec3 dir = glm::normalize(glm::vec3(x, radius / height, z));
    glm::vec3 tangent = { -z, 0.0f, x };
    glm::vec3 bitangent = glm::cross(dir, tangent);
    vertices.push_back(Vertex {
      .position = pos,
      .normal = dir,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
  }

  // --- Fix: Duplicate rim vertices for base cap with correct normals ---
  // This ensures base cap triangles use only vertices with downward normals,
  // so shading is correct and not interpolated with side normals.
  std::vector<uint32_t> base_cap_rim_indices;
  for (unsigned int i = 0; i < segments; ++i) {
    float theta = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    float u = (x + 1.0f) * 0.5f;
    float v = (z + 1.0f) * 0.5f;
    vertices.push_back(Vertex {
      .position = { x * radius, -half_height, z * radius },
      .normal = { 0, -1, 0 }, // Downward normal for base cap
      .texcoord = { u, v },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 0, 1 },
      .color = { 1, 1, 1, 1 },
    });
    base_cap_rim_indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
  }
  // Apex vertex
  vertices.push_back(Vertex {
    .position = apex,
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.0f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = { 1, 1, 1, 1 },
  });
  uint32_t apex_index = static_cast<uint32_t>(vertices.size() - 1);
  // Side indices
  // Ensure side triangles are wound CCW when viewed from outside so
  // the computed normals point outward. Use (base_current, apex, base_next).
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(i);
    indices.push_back(apex_index);
    indices.push_back(i + 1);
  }
  // Center vertex for base cap
  vertices.push_back(Vertex {
    .position = { 0, -half_height, 0 },
    .normal = { 0, -1, 0 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = { 1, 1, 1, 1 },
  });
  uint32_t base_center = static_cast<uint32_t>(vertices.size() - 1);

  // --- Fix: Use duplicated rim vertices for base cap ---
  // This ensures all base cap triangles use only vertices with correct downward normals.
  for (unsigned int i = 0; i < segments; ++i) {
    uint32_t v0 = base_cap_rim_indices[i];
    uint32_t v1 = base_cap_rim_indices[(i + 1) % segments];
    indices.push_back(v0);
    indices.push_back(v1);
    indices.push_back(base_center);
  }

  return { { std::move(vertices), std::move(indices) } };
}
