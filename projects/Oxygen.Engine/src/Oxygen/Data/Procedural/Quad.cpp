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
 Creates vertex and index buffers for a rectangle in the XY plane at z = 0,
 centred at the origin. The quad has two triangles, normals along +Z, and
 texture coordinates spanning [0,1] across its width and height.

 @param width Width along X (must be > 0).
 @param height Height along Y (must be > 0).
 @return Vertex and index vectors, or std::nullopt for
 non-positive width or height.

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
  if (width <= 0.0f || height <= 0.0f) {
    return std::nullopt;
  }
  float half_w = width * 0.5f;
  float half_h = height * 0.5f;
  std::vector<Vertex> vertices = {
    // clang-format off
    { { -half_w, -half_h, 0.0f }, { 0, 0, 1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 1, 1 } },
    { {  half_w, -half_h, 0.0f }, { 0, 0, 1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 1, 1 } },
    { {  half_w,  half_h, 0.0f }, { 0, 0, 1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 1, 1 } },
    { { -half_w,  half_h, 0.0f }, { 0, 0, 1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 1, 1 } },
    // clang-format on
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
