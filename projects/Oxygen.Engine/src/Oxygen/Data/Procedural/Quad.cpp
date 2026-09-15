//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <cmath>
#include <string_view>
#include <vector>

/*!
 Creates vertex and index buffers for a rectangle in the XZ plane at y = 0,
 centred at the origin. The quad has two triangles, normals along -Y, and
 texture coordinates spanning [0,1] across its width and height.

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
  std::vector<Vertex> vertices = {
    { .position = { -half_w, 0.0F, -half_h },
      .normal = { 0, -1, 0 },
      .texcoord = { 0, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 0, -1 },
      .color = { 1, 1, 1, 1 } },
    { .position = { half_w, 0.0F, -half_h },
      .normal = { 0, -1, 0 },
      .texcoord = { 1, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 0, -1 },
      .color = { 1, 1, 1, 1 } },
    { .position = { half_w, 0.0F, half_h },
      .normal = { 0, -1, 0 },
      .texcoord = { 1, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 0, -1 },
      .color = { 1, 1, 1, 1 } },
    { .position = { -half_w, 0.0F, half_h },
      .normal = { 0, -1, 0 },
      .texcoord = { 0, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 0, -1 },
      .color = { 1, 1, 1, 1 } },
  };
  std::vector<uint32_t> indices = {
    0,
    1,
    2, // triangle 1
    0,
    2,
    3, // triangle 2
  };

  return { { std::move(vertices), std::move(indices) } };
}
