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

#include <Oxygen/Data/Procedural/Revolution.h>
#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

/*!
 Creates vertex and index buffers for a closed cylinder along the Z axis, with
 caps at z = -height/2 and z = +height/2 and its axis through x = y = 0. The
 side follows the revolution layout of `space::texture` (seam at the back,
 v = 0 at the top), the caps the top and bottom face layouts.

 @param segments Number of radial segments (minimum 3).
 @param height Finite positive height along Z.
 @param radius Finite positive radius in the XY plane.
 @return Vertex and index vectors, or std::nullopt for
 invalid counts/dimensions or sampled edges that collapse in float32 storage.

 ### Performance Characteristics

 - Time Complexity: O(segments).
 - Output: 4*segments+4 vertices and 12*segments indices.
 - Side and cap rim vertices are separate to preserve their normal/UV seams.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeCylinderMeshAsset(32, 1.0f, 0.5f)) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, procedural::AppendCylinder
*/
auto oxygen::data::MakeCylinderMeshAsset(
  const unsigned int segments, const float height, const float radius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr auto kIndicesPerSegment = 12U;
  const auto half_height = height / 2.0F;
  if (segments < 3
    || segments > std::numeric_limits<uint32_t>::max() / kIndicesPerSegment
    || !std::isfinite(height) || !std::isfinite(radius) || half_height <= 0.0F
    || radius <= 0.0F) {
    return std::nullopt;
  }

  auto buffers = procedural::MeshBuffers {};
  if (!procedural::AppendCylinder(buffers,
        procedural::AxialExtent {
          .bottom_z = -half_height,
          .top_z = half_height,
          .radius = radius,
        },
        procedural::SolidStyle {
          .segments = segments,
          .color = { 1.0F, 1.0F, 1.0F, 1.0F },
        })) {
    return std::nullopt;
  }
  return std::make_pair(
    std::move(buffers.vertices), std::move(buffers.indices));
}
