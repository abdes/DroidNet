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
 Creates a new Mesh representing a unit axis-aligned cube centered at the
 origin.

 @return Shared pointer to the immutable Mesh containing the cube geometry.
 Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(1) (fixed-size geometry generation)
 - Memory: Allocates space for 8 vertices and 36 indices
 - Optimization: No dynamic allocations beyond vector growth; all data is
   constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a cube mesh asset
 auto cube = MakeCubeMeshAsset();
 for (const auto& v : cube->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
       using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto oxygen::data::MakeCubeMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  // Vertices for a unit cube centered at the origin, 24 vertices (4 per face)
  constexpr float h = 0.5f;
  std::vector<Vertex> vertices = {
    // clang-format off
    // -Z face (bottom)
    { { -h, -h, -h }, {  0,  0, -1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h, -h, -h }, {  0,  0, -1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h,  h, -h }, {  0,  0, -1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { { -h,  h, -h }, {  0,  0, -1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    // +Z face (top)
    { { -h, -h,  h }, {  0,  0,  1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h, -h,  h }, {  0,  0,  1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h,  h,  h }, {  0,  0,  1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { { -h,  h,  h }, {  0,  0,  1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    // -X face (left)
    { { -h, -h, -h }, { -1,  0,  0 }, { 0, 0 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { { -h,  h, -h }, { -1,  0,  0 }, { 1, 0 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { { -h,  h,  h }, { -1,  0,  0 }, { 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { { -h, -h,  h }, { -1,  0,  0 }, { 0, 1 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    // +X face (right)
    { {  h, -h, -h }, {  1,  0,  0 }, { 0, 0 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h,  h, -h }, {  1,  0,  0 }, { 1, 0 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h,  h,  h }, {  1,  0,  0 }, { 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h, -h,  h }, {  1,  0,  0 }, { 0, 1 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    // -Y face (front)
    { { -h, -h, -h }, {  0, -1,  0 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h, -h, -h }, {  0, -1,  0 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h, -h,  h }, {  0, -1,  0 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { { -h, -h,  h }, {  0, -1,  0 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    // +Y face (back)
    { { -h,  h, -h }, {  0,  1,  0 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h,  h, -h }, {  0,  1,  0 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { {  h,  h,  h }, {  0,  1,  0 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    { { -h,  h,  h }, {  0,  1,  0 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } },
    // clang-format off
  };
  std::vector<uint32_t> indices = {
    // clang-format off
    // -Z face (bottom) - triangle winding must produce outward normal = -Z
    0, 2, 1, 0, 3, 2,
    // +Z face (top)
    4, 5, 6, 4, 6, 7,
    // -X face (left) - triangle winding must produce outward normal = -X
    8, 10, 9, 8, 11, 10,
    // +X face (right)
    12, 13, 14, 12, 14, 15,
    // -Y face (front)
    16, 17, 18, 16, 18, 19,
    // +Y face (back) - triangle winding must produce outward normal = +Y
    20, 22, 21, 20, 23, 22,
    // clang-format off
  };

  return { { std::move(vertices), std::move(indices) } };
}
