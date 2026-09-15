//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

/*!
 Creates vertex and index buffers for a torus centred at the origin. The main
 ring lies in the XY plane around the Z axis. A circle of minor_radius is
 swept around a circle of major_radius; normals, UVs, tangents, bitangents and
 colours accompany positions. The default major/minor radii are 0.4/0.1 m,
 giving a 1 m outer diameter and a 0.2 m tube diameter. Positive radii also
 support horn/spindle parameterizations; those are not regular ring surfaces.

 @param major_segments Number of intervals around the main ring (minimum 3).
 @param minor_segments Number of intervals around the tube (minimum 3).
 @param major_radius Finite positive distance from origin to tube centre.
 @param minor_radius Finite positive tube radius.
 @return Vertex and index vectors, or std::nullopt for
 invalid counts/radii, an overflowing outer radius, or a tube radius that
 collapses against the major radius in float32 storage.

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

 @see GenerateMesh, Vertex
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
    || minor_radius <= 0.0F || outer_radius > std::numeric_limits<float>::max()
    || major_radius + minor_radius <= major_radius
    || major_radius - minor_radius >= major_radius
    || static_cast<uint64_t>(major_segments) + 1U
      > max_count / (static_cast<uint64_t>(minor_segments) + 1U)
    || static_cast<uint64_t>(major_segments) * minor_segments
      > max_count / kIndicesPerCell) {
    return std::nullopt;
  }
  constexpr double pi = std::numbers::pi_v<double>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  for (unsigned int i = 0; i <= major_segments; ++i) {
    const auto major_theta
      = i == major_segments ? 0.0 : 2.0 * pi * i / major_segments;
    const auto cos_major = static_cast<float>(std::cos(major_theta));
    const auto sin_major = static_cast<float>(std::sin(major_theta));

    for (unsigned int j = 0; j <= minor_segments; ++j) {
      const auto minor_theta
        = j == minor_segments ? 0.0 : 2.0 * pi * j / minor_segments;
      const auto cos_minor = static_cast<float>(std::cos(minor_theta));
      const auto sin_minor = static_cast<float>(std::sin(minor_theta));

      glm::vec3 pos = { cos_major * (major_radius + minor_radius * cos_minor),
        sin_major * (major_radius + minor_radius * cos_minor),
        minor_radius * sin_minor };
      // Analytic direction remains finite at singular horn/spindle samples
      // and avoids subtracting nearly equal large position coordinates.
      glm::vec3 normal
        = { cos_major * cos_minor, sin_major * cos_minor, sin_minor };
      glm::vec2 texcoord
        = { static_cast<float>(i) / static_cast<float>(major_segments),
            static_cast<float>(j) / static_cast<float>(minor_segments) };

      glm::vec3 tangent = { -sin_major, cos_major, 0.0f };
      glm::vec3 bitangent
        = { -cos_major * sin_minor, -sin_major * sin_minor, cos_minor };

      vertices.push_back(Vertex {
        .position = pos,
        .normal = normal,
        .texcoord = texcoord,
        .tangent = tangent,
        .bitangent = bitangent,
        .color = { 1, 1, 1, 1 },
      });
    }
  }

  for (unsigned int i = 0; i < major_segments; ++i) {
    for (unsigned int j = 0; j < minor_segments; ++j) {
      uint32_t i0 = i * (minor_segments + 1) + j;
      uint32_t i1 = (i + 1) * (minor_segments + 1) + j;
      uint32_t i2 = i0 + 1;
      uint32_t i3 = i1 + 1;

      // CCW: (curr_maj, next_maj, curr_maj_next_min)
      indices.push_back(i0);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i3);
    }
  }

  return { { std::move(vertices), std::move(indices) } };
}
