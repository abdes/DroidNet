//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

/*!
 Creates a unit cube centred at the origin whose faces are grids of
 `segments` x `segments` cells. Each face has its own vertices, flat normal
 and the face layout of `space::texture` ("UV layout of generated surfaces"),
 so a texture reads upright and unmirrored on every face; faces sharing an
 edge share its positions exactly, so welding by position closes the surface.

 @param segments Cells along each face edge, in [1, 64].
 @return Vertex and index vectors, or std::nullopt for an invalid count.

 ### Performance Characteristics

 - Time Complexity: O(segments^2).
 - Output: 6*(segments+1)^2 vertices and 36*segments^2 indices.

 @see MakeCubeMeshAsset, procedural::kBoxFaces
*/
auto oxygen::data::MakeSubdividedCubeMeshAsset(const unsigned int segments)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr unsigned int kMaxSegments = 64U;
  constexpr auto kHalfExtent = 0.5F;
  if (segments == 0U || segments > kMaxSegments) {
    return std::nullopt;
  }

  const auto stride = segments + 1U;
  auto buffers = procedural::MeshBuffers {};
  buffers.vertices.reserve(procedural::kBoxFaces.size() * stride * stride);
  buffers.indices.reserve(
    procedural::kBoxFaces.size() * segments * segments * 6U);

  for (const auto& face : procedural::kBoxFaces) {
    const auto first = static_cast<uint32_t>(buffers.vertices.size());
    for (uint32_t v = 0; v <= segments; ++v) {
      for (uint32_t u = 0; u <= segments; ++u) {
        buffers.vertices.push_back(Vertex {
          .position = (face.normal * kHalfExtent)
            + (face.tangent * procedural::GridOffset(u, segments))
            + (face.bitangent * procedural::GridOffset(v, segments)),
          .normal = face.normal,
          .texcoord = {
            procedural::GridCoordinate(u, segments),
            procedural::GridCoordinate(v, segments),
          },
          .tangent = face.tangent,
          .bitangent = face.bitangent,
          .color = { 1.0F, 1.0F, 1.0F, 1.0F },
        });
      }
    }
    // cross(B, T) = N on an unmirrored face, so (p(u, v), p(u, v + 1),
    // p(u + 1, v)) winds counter-clockwise seen from outside.
    for (uint32_t v = 0; v < segments; ++v) {
      for (uint32_t u = 0; u < segments; ++u) {
        const auto top_left = first + (v * stride) + u;
        const auto bottom_left = top_left + stride;
        buffers.indices.insert(buffers.indices.end(),
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
  }

  return std::make_pair(
    std::move(buffers.vertices), std::move(buffers.indices));
}
