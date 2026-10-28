//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/Procedural/Revolution.h>
#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

/*!
 Creates vertex and index buffers for an arrow used in editor-axis and debug
 visualization: a closed blue cylinder shaft and a closed yellow cone head
 pointing along +Z, built like `MakeCylinderMeshAsset` and
 `MakeConeMeshAsset`. The arrow is centred on the Z axis, with its shaft base
 at z = -0.1 and tip at z = 0.78; its bounding box is not centred at the
 origin.

 @return A pair of vertex and index vectors wrapped in an engaged optional.

 ### Performance Characteristics

 - Time Complexity: O(1), using a fixed 16 radial segments.
 - Output: 118 vertices and 288 indices (96 triangles), two closed parts.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeArrowGizmoMeshAsset()) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, procedural::AppendCylinder, procedural::AppendCone
*/
auto oxygen::data::MakeArrowGizmoMeshAsset()
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr uint32_t kSegments = 16U;
  constexpr float kBaseZ = -0.1F;
  constexpr float kShaftTopZ = kBaseZ + 0.7F;
  constexpr float kHeadTopZ = kShaftTopZ + 0.18F;
  constexpr float kShaftRadius = 0.025F;
  constexpr float kHeadRadius = 0.06F;
  constexpr Vec4 kShaftColor { 0.2F, 0.6F, 1.0F, 1.0F };
  constexpr Vec4 kHeadColor { 1.0F, 0.8F, 0.2F, 1.0F };

  auto buffers = procedural::MeshBuffers {};
  const auto shaft = procedural::AppendCylinder(buffers,
    procedural::AxialExtent {
      .bottom_z = kBaseZ,
      .top_z = kShaftTopZ,
      .radius = kShaftRadius,
    },
    procedural::SolidStyle { .segments = kSegments, .color = kShaftColor });
  const auto head = procedural::AppendCone(buffers,
    procedural::AxialExtent {
      .bottom_z = kShaftTopZ,
      .top_z = kHeadTopZ,
      .radius = kHeadRadius,
    },
    procedural::SolidStyle { .segments = kSegments, .color = kHeadColor });
  if (!shaft || !head) {
    return std::nullopt;
  }
  return std::make_pair(
    std::move(buffers.vertices), std::move(buffers.indices));
}
