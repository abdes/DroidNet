//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

/*!
 Creates vertex and index buffers for a rectangle in the XZ plane at y = 0,
 centred at the origin, facing the front (-Y). The quad has two triangles and
 the front face layout of `space::texture`: u spans the width along +X and v
 the height downward, so a texture reads upright and unmirrored from the
 front.

 @param width Finite positive width along X.
 @param height Finite positive height along Z.
 @return Vertex and index vectors, or std::nullopt for
 non-finite/non-positive dimensions or a half-extent that collapses in float32.

 ### Performance Characteristics

 - Time Complexity: O(1).
 - Output: 4 vertices and 6 indices (2 triangles).

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeQuadMeshAsset(2.0f, 1.0f)) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, Vertex
*/
auto oxygen::data::MakeQuadMeshAsset(const float width, const float height)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  const auto half_w = width / 2.0F;
  const auto half_h = height / 2.0F;
  if (!std::isfinite(width) || !std::isfinite(height) || half_w <= 0.0F
    || half_h <= 0.0F) {
    return std::nullopt;
  }
  // The front face layout: +U along the width, +V down the height.
  const auto& face = procedural::kFrontFace;
  std::vector<Vertex> vertices;
  vertices.reserve(4U);
  for (const auto v : { 0.0F, 1.0F }) {
    for (const auto u : { 0.0F, 1.0F }) {
      vertices.push_back(Vertex {
        .position = (face.tangent * ((u - 0.5F) * width))
          + (face.bitangent * ((v - 0.5F) * height)),
        .normal = face.normal,
        .texcoord = { u, v },
        .tangent = face.tangent,
        .bitangent = face.bitangent,
        .color = { 1, 1, 1, 1 },
      });
    }
  }
  // (p(0, 0), p(0, 1), p(1, 0)) winds counter-clockwise seen from the front.
  std::vector<uint32_t> indices = { 0, 2, 1, 1, 2, 3 };

  return { { std::move(vertices), std::move(indices) } };
}
