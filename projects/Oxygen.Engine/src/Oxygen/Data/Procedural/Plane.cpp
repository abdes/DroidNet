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
 Creates vertex and index buffers for a square grid in the XY plane at z = 0,
 centred at the origin. Every grid cell contains two triangles, with normals
 along +Z and texture coordinates spanning [0,1] across the complete surface.

 @param x_segments Number of grid cells along X (minimum 1).
 @param y_segments Number of grid cells along Y (minimum 1).
 @param size Length along both X and Y (must be > 0).
 @return Vertex and index vectors, or std::nullopt for
 segment counts below one or a non-positive size.

 ### Performance Characteristics

 - Time Complexity: O(x_segments * y_segments).
 - Output: (x_segments+1)*(y_segments+1) vertices and
   6*x_segments*y_segments indices.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakePlaneMeshAsset(2, 2, 1.0f)) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, Vertex
*/
auto oxygen::data::MakePlaneMeshAsset(
  unsigned int x_segments, unsigned int y_segments, float size)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  if (x_segments < 1 || y_segments < 1 || size <= 0.0f) {
    return std::nullopt;
  }
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  float half_size = size * 0.5f;
  for (unsigned int y = 0; y <= y_segments; ++y) {
    float y_frac = static_cast<float>(y) / static_cast<float>(y_segments);
    float y_pos = -half_size + y_frac * size;
    for (unsigned int x = 0; x <= x_segments; ++x) {
      float x_frac = static_cast<float>(x) / static_cast<float>(x_segments);
      float x_pos = -half_size + x_frac * size;
      Vertex v {
        .position = { x_pos, y_pos, 0.0f },
        .normal = { 0.0f, 0.0f, 1.0f },
        .texcoord = { x_frac, 1.0f - y_frac },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = { 0.0f, 1.0f, 0.0f },
        .color = { 1, 1, 1, 1 },
      };
      vertices.push_back(v);
    }
  }
  for (unsigned int y = 0; y < y_segments; ++y) {
    for (unsigned int x = 0; x < x_segments; ++x) {
      uint32_t i0 = y * (x_segments + 1) + x;
      uint32_t i1 = i0 + 1;
      uint32_t i2 = i0 + (x_segments + 1);
      uint32_t i3 = i2 + 1;
      // CCW: (BL, BR, TL) and (TL, BR, TR)
      indices.push_back(i0);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i3);
    }
  }

  return { { std::move(vertices), std::move(indices) } };
}
