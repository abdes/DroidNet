//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <string_view>
#include <vector>

/*!
 Creates a new Mesh representing a flat plane in the XZ plane centered at
 the origin. The plane is subdivided into a grid of quads, with each quad made
 of two triangles. Vertices are generated with positions, normals, texcoords,
 tangents, bitangents, and color.

 @param x_segments Number of subdivisions along the X axis (minimum 1).
 @param z_segments Number of subdivisions along the Z axis (minimum 1).
 @param size Length of the plane along X and Z (plane is size x size).
 @return Shared pointer to the immutable Mesh containing the plane
 geometry. Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(x_segments * z_segments)
 - Memory: Allocates space for (x_segments+1)*(z_segments+1) vertices and
 6*x_segments*z_segments indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a 2x2 plane mesh asset of size 1.0
 auto plane = MakePlaneMeshAsset(2, 2, 1.0f);
 for (const auto& v : plane->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
 using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto oxygen::data::MakePlaneMeshAsset(
  unsigned int x_segments, unsigned int z_segments, float size)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  if (x_segments < 1 || z_segments < 1 || size <= 0.0f) {
    return std::nullopt;
  }
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  float half_size = size * 0.5f;
  for (unsigned int z = 0; z <= z_segments; ++z) {
    float z_frac = static_cast<float>(z) / static_cast<float>(z_segments);
    float z_pos = -half_size + z_frac * size;
    for (unsigned int x = 0; x <= x_segments; ++x) {
      float x_frac = static_cast<float>(x) / static_cast<float>(x_segments);
      float x_pos = -half_size + x_frac * size;
      Vertex v {
        .position = { x_pos, 0.0f, z_pos },
        .normal = { 0.0f, 1.0f, 0.0f },
        .texcoord = { x_frac, 1.0f - z_frac },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = { 0.0f, 0.0f, 1.0f },
        .color = { 1, 1, 1, 1 },
      };
      vertices.push_back(v);
    }
  }
  for (unsigned int z = 0; z < z_segments; ++z) {
    for (unsigned int x = 0; x < x_segments; ++x) {
      uint32_t i0 = z * (x_segments + 1) + x;
      uint32_t i1 = i0 + 1;
      uint32_t i2 = i0 + (x_segments + 1);
      uint32_t i3 = i2 + 1;
      // Wind triangles CCW when viewed from +Y (up) so the normal {0,1,0}
      // matches the vertex winding and back-face culling behaves correctly.
      // First triangle: bottom-left, bottom-right, top-left
      indices.push_back(i0);
      indices.push_back(i1);
      indices.push_back(i2);
      // Second triangle: bottom-right, top-right, top-left
      indices.push_back(i1);
      indices.push_back(i3);
      indices.push_back(i2);
    }
  }

  return { { std::move(vertices), std::move(indices) } };
}
