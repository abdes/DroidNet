//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

/*!
 Creates vertex and index buffers for a ring torus centred at the origin. The
 main ring lies in the XY plane around the Z axis. A circle of minor_radius is
 swept around a circle of major_radius. The default major/minor radii are
 0.4/0.1 m, giving a 1 m outer diameter and a 0.2 m tube diameter.

 It follows the torus layout of `space::texture`: u runs around the main ring
 with its seam at the back, and v runs around the tube from the outer equator
 down the outer wall, under the tube, up the inner wall and over the top, so a
 texture reads upright and unmirrored on the outer wall.

 @param major_segments Number of intervals around the main ring (minimum 3).
 @param minor_segments Number of intervals around the tube (minimum 3).
 @param major_radius Finite positive distance from origin to tube centre.
 @param minor_radius Finite positive tube radius, smaller than major_radius.
 @return Vertex and index vectors, or std::nullopt for invalid counts/radii, a
 horn or spindle torus (minor_radius >= major_radius, which self-intersects),
 an overflowing outer radius, or a tube radius that collapses against the
 major radius in float32 storage.

 ### Performance Characteristics

 - Time Complexity: O(major_segments * minor_segments).
 - Output: (major_segments+1)*(minor_segments+1) vertices and
   6*major_segments*minor_segments indices.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeTorusMeshAsset(32, 16, 0.4f, 0.1f)) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, procedural::RingAt
*/
auto oxygen::data::MakeTorusMeshAsset(unsigned int major_segments,
  unsigned int minor_segments, float major_radius, float minor_radius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  constexpr auto max_count = std::numeric_limits<uint32_t>::max();
  constexpr auto kIndicesPerCell = 6U;
  const auto outer_radius = static_cast<double>(major_radius) + minor_radius;
  if (major_segments < 3 || minor_segments < 3 || !std::isfinite(major_radius)
    || !std::isfinite(minor_radius) || major_radius <= 0.0F
    || minor_radius <= 0.0F || minor_radius >= major_radius
    || outer_radius > std::numeric_limits<float>::max()
    || major_radius + minor_radius <= major_radius
    || major_radius - minor_radius >= major_radius
    || static_cast<uint64_t>(major_segments) + 1U
      > max_count / (static_cast<uint64_t>(minor_segments) + 1U)
    || static_cast<uint64_t>(major_segments) * minor_segments
      > max_count / kIndicesPerCell) {
    return std::nullopt;
  }

  constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
  const auto stride = minor_segments + 1U;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(static_cast<size_t>(major_segments + 1U) * stride);
  indices.reserve(
    static_cast<size_t>(major_segments) * minor_segments * kIndicesPerCell);

  for (uint32_t i = 0; i <= major_segments; ++i) {
    const auto ring = procedural::RingAt(i, major_segments);
    for (uint32_t j = 0; j <= minor_segments; ++j) {
      // The tube angle above the outer equator: v grows as it decreases, down
      // the outer wall first. Column `minor_segments` repeats column 0.
      const auto tube_angle = -kTwoPi * static_cast<double>(j % minor_segments)
        / static_cast<double>(minor_segments);
      const auto cos_tube = static_cast<float>(std::cos(tube_angle));
      const auto sin_tube = static_cast<float>(std::sin(tube_angle));
      vertices.push_back(Vertex {
        .position = (ring.outward * (major_radius + (minor_radius * cos_tube)))
          + (space::move::Up * (minor_radius * sin_tube)),
        .normal = (ring.outward * cos_tube) + (space::move::Up * sin_tube),
        .texcoord = {
          procedural::GridCoordinate(i, major_segments),
          procedural::GridCoordinate(j, minor_segments),
        },
        .tangent = ring.tangent,
        .bitangent = (ring.outward * sin_tube) - (space::move::Up * cos_tube),
        .color = { 1.0F, 1.0F, 1.0F, 1.0F },
      });
    }
  }

  // (p(u, v), p(u, v + 1), p(u + 1, v)) winds counter-clockwise seen from
  // outside: +V runs down the outer wall and +U counter-clockwise from +Z.
  for (uint32_t i = 0; i < major_segments; ++i) {
    for (uint32_t j = 0; j < minor_segments; ++j) {
      const auto current = (i * stride) + j;
      const auto next_ring = current + stride;
      indices.insert(indices.end(),
        {
          current,
          current + 1U,
          next_ring,
          next_ring,
          current + 1U,
          next_ring + 1U,
        });
    }
  }

  return { { std::move(vertices), std::move(indices) } };
}
