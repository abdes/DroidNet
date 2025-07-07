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
 Creates a new Mesh representing a quad (rectangle) in the XZ plane,
 centered at the origin. The quad is made of two triangles. Vertices are
 generated with positions, normals, texcoords, tangents, bitangents, and color.

 @param width Width of the quad along the X axis (must be > 0).
 @param height Height of the quad along the Z axis (must be > 0).
 @return Shared pointer to the immutable Mesh containing the quad geometry.
 Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: Allocates space for 4 vertices and 6 indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a quad mesh asset
auto quad = MakeQuadMeshAsset(2.0f, 1.0f);
for (const auto& v : quad->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
       using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
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
    { { -half_w, 0.0f, -half_h }, { 0, 1, 0 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1, 1 } },
    { {  half_w, 0.0f, -half_h }, { 0, 1, 0 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1, 1 } },
    { {  half_w, 0.0f,  half_h }, { 0, 1, 0 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1, 1 } },
    { { -half_w, 0.0f,  half_h }, { 0, 1, 0 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1, 1 } },
    // clang-format on
  };
  std::vector<uint32_t> indices = {
    0, 1, 2, // triangle 1
    2, 3, 0, // triangle 2
  };

  return { { std::move(vertices), std::move(indices) } };
}
