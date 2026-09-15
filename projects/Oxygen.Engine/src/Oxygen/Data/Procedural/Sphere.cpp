//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <numbers>
#include <string_view>
#include <vector>

/*!
 Creates vertex and index buffers for a UV sphere of radius 0.5, centred at
 the origin. Latitude runs between the +Z and -Z poles; longitude runs around
 the Z axis. Normals, UVs, tangents, bitangents and colours accompany positions.

 @param latitude_segments Number of intervals between the poles (minimum 3).
 @param longitude_segments Number of intervals around the equator (minimum 3).
 @return Vertex and index vectors, or std::nullopt for
 segment counts below three.

 ### Performance Characteristics

 - Time Complexity: O(latitude_segments * longitude_segments).
 - Output: (latitude_segments+1)*(longitude_segments+1) vertices and
   6*latitude_segments*longitude_segments indices, including pole degeneracies.
 - The longitude seam and pole vertices are duplicated for UV coordinates.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeSphereMeshAsset(16, 32)) {
   const auto& [vertices, indices] = *buffers;
   // Pass the buffers to the mesh consumer.
 }
 ```

 @see GenerateMesh, Vertex
*/
auto oxygen::data::MakeSphereMeshAsset(
  unsigned int latitude_segments, unsigned int longitude_segments)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  if (latitude_segments < 3 || longitude_segments < 3) {
    return std::nullopt;
  }
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  constexpr float pi = std::numbers::pi_v<float>;

  for (unsigned int lat = 0; lat <= latitude_segments; ++lat) {
    float theta
      = pi * static_cast<float>(lat) / static_cast<float>(latitude_segments);
    float sin_theta = std::sin(theta);
    float cos_theta = std::cos(theta);

    for (unsigned int lon = 0; lon <= longitude_segments; ++lon) {
      float phi = 2.0f * pi * static_cast<float>(lon)
        / static_cast<float>(longitude_segments);
      float sin_phi = std::sin(phi);
      float cos_phi = std::cos(phi);

      float x = sin_theta * cos_phi;
      float y = sin_theta * sin_phi;
      float z = cos_theta; // Z is UP

      Vertex v {
        .position = { x * 0.5f, y * 0.5f, z * 0.5f },
        .normal = { x, y, z },
        .texcoord
        = { static_cast<float>(lon) / static_cast<float>(longitude_segments),
          1.0f
            - static_cast<float>(lat) / static_cast<float>(latitude_segments) },
        .tangent = { -sin_phi, cos_phi, 0.0f },
        .bitangent = { -cos_theta * cos_phi, -cos_theta * sin_phi, sin_theta },
        .color = { 1, 1, 1, 1 },
      };
      vertices.push_back(v);
    }
  }

  for (unsigned int lat = 0; lat < latitude_segments; ++lat) {
    for (unsigned int lon = 0; lon < longitude_segments; ++lon) {
      uint32_t i0 = lat * (longitude_segments + 1) + lon;
      uint32_t i1 = (lat + 1) * (longitude_segments + 1) + lon;
      uint32_t i2 = i0 + 1;
      uint32_t i3 = i1 + 1;

      // CCW: (TL, BL, TR) and (TR, BL, BR)
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
