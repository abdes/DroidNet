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
#include <string_view>
#include <vector>

/*!
 Creates vertex and index buffers for a square grid in the XY plane at z = 0,
 centred at the origin. Every grid cell contains two triangles, with normals
 along +Z and texture coordinates spanning [0,1] across the complete surface.

 @param x_segments Number of grid cells along X (minimum 1).
 @param y_segments Number of grid cells along Y (minimum 1).
 @param size Finite positive length along both X and Y.
 @return Vertex and index vectors, or std::nullopt for
 invalid counts/dimensions or grid cells that collapse in float32 storage.

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
  constexpr auto max_count = std::numeric_limits<uint32_t>::max();
  constexpr auto kIndicesPerCell = 6U;
  constexpr auto kUnitHalfExtent = 0.5;
  const auto half_size = size / 2.0F;
  if (x_segments < 1 || y_segments < 1 || !std::isfinite(size)
    || half_size <= 0.0F
    || static_cast<uint64_t>(x_segments) + 1U
      > max_count / (static_cast<uint64_t>(y_segments) + 1U)
    || static_cast<uint64_t>(x_segments) * y_segments
      > max_count / kIndicesPerCell) {
    return std::nullopt;
  }
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  for (unsigned int y = 0; y <= y_segments; ++y) {
    float y_frac = static_cast<float>(y) / static_cast<float>(y_segments);
    const auto y_pos = static_cast<float>(
      ((static_cast<double>(y) / y_segments) - kUnitHalfExtent) * size);
    if (y > 0 && y_pos <= vertices.back().position.y) {
      return std::nullopt;
    }
    for (unsigned int x = 0; x <= x_segments; ++x) {
      float x_frac = static_cast<float>(x) / static_cast<float>(x_segments);
      const auto x_pos = static_cast<float>(
        ((static_cast<double>(x) / x_segments) - kUnitHalfExtent) * size);
      if (x > 0 && x_pos <= vertices.back().position.x) {
        return std::nullopt;
      }
      Vertex v {
        .position = { x_pos, y_pos, 0.0f },
        .normal = { 0.0f, 0.0f, 1.0f },
        .texcoord = { x_frac, 1.0f - y_frac },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = { 0.0F, -1.0F, 0.0F },
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
