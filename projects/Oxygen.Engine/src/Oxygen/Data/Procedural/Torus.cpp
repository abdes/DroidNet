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
 Creates vertex and index buffers for a torus centred at the origin. The main
 ring lies in the XY plane around the Z axis. A circle of minor_radius is
 swept around a circle of major_radius; normals, UVs, tangents, bitangents and
 colours accompany positions.

 @param major_segments Number of intervals around the main ring (minimum 3).
 @param minor_segments Number of intervals around the tube (minimum 3).
 @param major_radius Distance from the origin to the tube centre (must be > 0).
 @param minor_radius Radius of the tube (must be > 0).
 @return Vertex and index vectors, or std::nullopt for
 segment counts below three or non-positive radii.

 ### Performance Characteristics

 - Time Complexity: O(major_segments * minor_segments).
 - Output: (major_segments+1)*(minor_segments+1) vertices and
   6*major_segments*minor_segments indices.

 ### Usage Example

 ```cpp
 if (auto buffers = oxygen::data::MakeTorusMeshAsset(32, 16, 1.0f, 0.25f)) {
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
  if (major_segments < 3 || minor_segments < 3 || major_radius <= 0.0f
    || minor_radius <= 0.0f) {
    return std::nullopt;
  }
  constexpr float pi = std::numbers::pi_v<float>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  for (unsigned int i = 0; i <= major_segments; ++i) {
    float major_theta
      = 2.0f * pi * static_cast<float>(i) / static_cast<float>(major_segments);
    float cos_major = std::cos(major_theta);
    float sin_major = std::sin(major_theta);
    glm::vec3 major_center
      = { major_radius * cos_major, major_radius * sin_major, 0.0f };

    for (unsigned int j = 0; j <= minor_segments; ++j) {
      float minor_theta = 2.0f * pi * static_cast<float>(j)
        / static_cast<float>(minor_segments);
      float cos_minor = std::cos(minor_theta);
      float sin_minor = std::sin(minor_theta);

      glm::vec3 pos = { cos_major * (major_radius + minor_radius * cos_minor),
        sin_major * (major_radius + minor_radius * cos_minor),
        minor_radius * sin_minor };
      glm::vec3 normal = glm::normalize(pos - major_center);
      glm::vec2 texcoord
        = { static_cast<float>(i) / static_cast<float>(major_segments),
            static_cast<float>(j) / static_cast<float>(minor_segments) };

      glm::vec3 tangent = { -sin_major, cos_major, 0.0f };
      glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));
      tangent = glm::normalize(glm::cross(bitangent, normal));

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
