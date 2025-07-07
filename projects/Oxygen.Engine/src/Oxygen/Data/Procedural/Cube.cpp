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
  // Vertices for a unit cube centered at the origin
  constexpr float h = 0.5f;
  std::vector<Vertex> vertices = {
    // clang-format off
    { { -h, -h, -h }, {  0,  0, -1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 0
    { {  h, -h, -h }, {  0,  0, -1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 1
    { {  h,  h, -h }, {  0,  0, -1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 2
    { { -h,  h, -h }, {  0,  0, -1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 3
    { { -h, -h,  h }, {  0,  0,  1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 4
    { {  h, -h,  h }, {  0,  0,  1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 5
    { {  h,  h,  h }, {  0,  0,  1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 6
    { { -h,  h,  h }, {  0,  0,  1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 7
    // clang-format on
  };
  std::vector<uint32_t> indices = {
    // clang-format off
    // -Z face (back)
    0, 2, 1, 0, 3, 2,
    // +Z face (front)
    4, 5, 6, 4, 6, 7,
    // -X face (left)
    0, 7, 3, 0, 4, 7,
    // +X face (right)
    1, 2, 6, 1, 6, 5,
    // -Y face (bottom)
    0, 1, 5, 0, 5, 4,
    // +Y face (top)
    3, 7, 6, 3, 6, 2,
    // clang-format on
  };

  return { { std::move(vertices), std::move(indices) } };
}
