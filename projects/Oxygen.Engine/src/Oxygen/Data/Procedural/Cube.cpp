//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

/*!
 Creates vertex and index buffers for a unit axis-aligned cube centred at the
 origin: the one-cell subdivided cube. Each face has separate vertices, a
 constant outward normal and the face layout of `space::texture`.

 @return A pair of vertex and index vectors wrapped in an engaged optional.

 ### Performance Characteristics

 - Time Complexity: O(1) (fixed-size geometry generation).
 - Output: 24 vertices and 36 indices (12 triangles).

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeCubeMeshAsset()) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, MakeSubdividedCubeMeshAsset, Vertex
*/
auto oxygen::data::MakeCubeMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  return MakeSubdividedCubeMeshAsset(1U);
}
