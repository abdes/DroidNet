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
 Creates a new Mesh representing a simple arrow gizmo, typically used for
 axis visualization in editors and debug views. The arrow is aligned along the
 +Y axis, composed of a cylinder shaft and a cone head, with distinct colors
 for shaft and head. All geometry is centered at the origin.

 @return Shared pointer to the immutable Mesh containing the arrow gizmo.
 Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(segments)
 - Memory: Allocates space for a small number of vertices and indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create an arrow gizmo mesh asset
auto arrow = MakeArrowGizmoMeshAsset();
for (const auto& v : arrow->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
       using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto oxygen::data::MakeArrowGizmoMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr unsigned int segments = 16;
  constexpr float shaft_length = 0.7f;
  constexpr float head_length = 0.18f;
  constexpr float base_y = -0.1f;
  constexpr float shaft_top_y = base_y + shaft_length;
  constexpr float head_top_y = shaft_top_y + head_length;
  constexpr glm::vec4 shaft_color = { 0.2f, 0.6f, 1.0f, 1.0f }; // blueish
  constexpr glm::vec4 head_color = { 1.0f, 0.8f, 0.2f, 1.0f }; // yellowish
  constexpr float pi = std::numbers::pi_v<float>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // Shaft (cylinder)
  for (unsigned int i = 0; i <= segments; ++i) {
    constexpr float shaft_radius = 0.025f;
    float theta
      = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    glm::vec3 normal = { x, 0.0f, z };
    glm::vec3 tangent = { -z, 0.0f, x };
    glm::vec3 bitangent = { 0.0f, 1.0f, 0.0f };
    float u = static_cast<float>(i) / static_cast<float>(segments);
    // Bottom ring
    vertices.push_back(Vertex {
      .position = { x * shaft_radius, base_y, z * shaft_radius },
      .normal = normal,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = shaft_color,
    });
    // Top ring
    vertices.push_back(Vertex {
      .position = { x * shaft_radius, shaft_top_y, z * shaft_radius },
      .normal = normal,
      .texcoord = { u, 0.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = shaft_color,
    });
  }
  // Shaft indices (side quads)
  for (unsigned int i = 0; i < segments; ++i) {
    uint32_t i0 = i * 2;
    uint32_t i1 = i0 + 1;
    uint32_t i2 = i0 + 2;
    uint32_t i3 = i0 + 3;
    indices.push_back(i0);
    indices.push_back(i2);
    indices.push_back(i1);
    indices.push_back(i1);
    indices.push_back(i2);
    indices.push_back(i3);
  }
  // Head (cone)
  uint32_t cone_base_start = static_cast<uint32_t>(vertices.size());
  for (unsigned int i = 0; i <= segments; ++i) {
    constexpr float head_radius = 0.06f;
    float theta
      = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    float u = static_cast<float>(i) / static_cast<float>(segments);
    glm::vec3 pos = { x * head_radius, shaft_top_y, z * head_radius };
    glm::vec3 dir = glm::normalize(glm::vec3(x, head_radius / head_length, z));
    glm::vec3 tangent = { -z, 0.0f, x };
    glm::vec3 bitangent = glm::cross(dir, tangent);
    vertices.push_back(Vertex {
      .position = pos,
      .normal = dir,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = head_color,
    });
  }
  // Cone apex
  vertices.push_back(Vertex {
    .position = { 0.0f, head_top_y, 0.0f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.0f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = head_color,
  });
  uint32_t apex_index = static_cast<uint32_t>(vertices.size() - 1);
  // Cone side indices
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(apex_index);
    indices.push_back(cone_base_start + i);
    indices.push_back(cone_base_start + i + 1);
  }
  // Cone base center
  vertices.push_back(Vertex {
    .position = { 0.0f, shaft_top_y, 0.0f },
    .normal = { 0.0f, -1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = head_color,
  });
  uint32_t base_center = static_cast<uint32_t>(vertices.size() - 1);
  // Cone base cap indices (CCW: i, i+1, center)
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(cone_base_start + i);
    indices.push_back(cone_base_start + i + 1);
    indices.push_back(base_center);
  }

  return { { std::move(vertices), std::move(indices) } };
}
