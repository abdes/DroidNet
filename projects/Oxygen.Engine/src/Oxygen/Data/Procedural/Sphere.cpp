//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Data/Procedural/Revolution.h>
#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

/*!
 Creates vertex and index buffers for a UV sphere of radius 0.5, centred at
 the origin, with its poles on the Z axis. It follows the revolution layout of
 `space::texture`: the seam at the back, u counter-clockwise seen from +Z and
 v = 0 at the +Z pole, so a texture reads upright and unmirrored from outside.

 @param latitude_segments Number of intervals between the poles (minimum 3).
 @param longitude_segments Number of intervals around the equator (minimum 3).
 @return Vertex and index vectors, or std::nullopt for
 segment counts below three or unrepresentable vertex/index counts.

 ### Performance Characteristics

 - Time Complexity: O(latitude_segments * longitude_segments).
 - Output: 2*longitude_segments + (latitude_segments-1)*(longitude_segments+1)
   vertices and 6*longitude_segments*(latitude_segments-1) indices.
 - The longitude seam is duplicated; each pole has one vertex per segment,
   at the segment's middle u, and no triangle is degenerate.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeSphereMeshAsset(16, 32)) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, procedural::AppendLatitudeSurface
*/
auto oxygen::data::MakeSphereMeshAsset(
  unsigned int latitude_segments, unsigned int longitude_segments)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr auto kMaxCount = std::numeric_limits<uint32_t>::max();
  constexpr auto kIndicesPerCell = 6U;
  constexpr auto kRadius = 0.5F;
  if (latitude_segments < 3 || longitude_segments < 3
    || (static_cast<uint64_t>(latitude_segments) + 1U)
      > kMaxCount / (static_cast<uint64_t>(longitude_segments) + 1U)
    || (static_cast<uint64_t>(latitude_segments) * longitude_segments)
      > kMaxCount / kIndicesPerCell) {
    return std::nullopt;
  }

  constexpr auto kPi = std::numbers::pi_v<double>;
  std::vector<procedural::Latitude> latitudes;
  latitudes.reserve(latitude_segments + 1U);
  for (uint32_t row = 0; row <= latitude_segments; ++row) {
    const auto theta
      = kPi * static_cast<double>(row) / static_cast<double>(latitude_segments);
    // The poles are exact: theta is 0 at the +Z pole and pi at the -Z pole.
    auto sin_theta = static_cast<float>(std::sin(theta));
    auto cos_theta = static_cast<float>(std::cos(theta));
    if (row == 0U) {
      sin_theta = 0.0F;
      cos_theta = 1.0F;
    } else if (row == latitude_segments) {
      sin_theta = 0.0F;
      cos_theta = -1.0F;
    }
    latitudes.push_back(procedural::Latitude {
      .ring_radius = kRadius * sin_theta,
      .z = kRadius * cos_theta,
      .sin_theta = sin_theta,
      .cos_theta = cos_theta,
      .v = procedural::GridCoordinate(row, latitude_segments),
    });
  }

  auto buffers = procedural::MeshBuffers {};
  if (!procedural::AppendLatitudeSurface(
        buffers, latitudes, longitude_segments)) {
    return std::nullopt;
  }
  return std::make_pair(
    std::move(buffers.vertices), std::move(buffers.indices));
}
