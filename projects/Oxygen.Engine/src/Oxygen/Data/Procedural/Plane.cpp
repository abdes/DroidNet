//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

/*!
 Creates vertex and index buffers for a square grid in the XY plane at z = 0,
 centred at the origin, facing +Z. Every grid cell contains two triangles.
 It has the top face layout of `space::texture`: u spans [0,1] along +X and v
 spans [0,1] toward the front (-Y), so a texture reads upright and unmirrored
 from above with its top toward the back.

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
  const auto half_size = size / 2.0F;
  if (x_segments < 1 || y_segments < 1 || !std::isfinite(size)
    || half_size <= 0.0F
    || static_cast<uint64_t>(x_segments) + 1U
      > max_count / (static_cast<uint64_t>(y_segments) + 1U)
    || static_cast<uint64_t>(x_segments) * y_segments
      > max_count / kIndicesPerCell) {
    return std::nullopt;
  }

  // The top face layout: +U along +X, +V toward the front (-Y), so the
  // image's top lies toward the back.
  const auto& face = procedural::kTopFace;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  for (uint32_t v = 0; v <= y_segments; ++v) {
    const auto v_offset = procedural::GridOffset(v, y_segments) * size;
    if (v > 0
      && v_offset <= procedural::GridOffset(v - 1U, y_segments) * size) {
      return std::nullopt;
    }
    for (uint32_t u = 0; u <= x_segments; ++u) {
      const auto u_offset = procedural::GridOffset(u, x_segments) * size;
      if (u > 0
        && u_offset <= procedural::GridOffset(u - 1U, x_segments) * size) {
        return std::nullopt;
      }
      vertices.push_back(Vertex {
        .position = (face.tangent * u_offset) + (face.bitangent * v_offset),
        .normal = face.normal,
        .texcoord = {
          procedural::GridCoordinate(u, x_segments),
          procedural::GridCoordinate(v, y_segments),
        },
        .tangent = face.tangent,
        .bitangent = face.bitangent,
        .color = { 1, 1, 1, 1 },
      });
    }
  }

  // (p(u, v), p(u, v + 1), p(u + 1, v)) winds counter-clockwise seen from
  // above.
  const auto stride = x_segments + 1U;
  for (uint32_t v = 0; v < y_segments; ++v) {
    for (uint32_t u = 0; u < x_segments; ++u) {
      const auto top_left = (v * stride) + u;
      const auto bottom_left = top_left + stride;
      indices.insert(indices.end(),
        {
          top_left,
          bottom_left,
          top_left + 1U,
          top_left + 1U,
          bottom_left,
          bottom_left + 1U,
        });
    }
  }

  return { { std::move(vertices), std::move(indices) } };
}
