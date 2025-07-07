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
 Creates a new Mesh representing a UV sphere centered at the origin.
 The sphere is generated using latitude and longitude segments, with vertices
 distributed over the surface and indexed triangles forming the mesh. Normals,
 UVs, tangents, bitangents, and vertex colors are set for each vertex.

 @param latitude_segments Number of segments along the vertical axis (minimum
 3).
 @param longitude_segments Number of segments around the equator (minimum 3).
 @return Shared pointer to the immutable Mesh containing the sphere
 geometry. Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(latitude_segments * longitude_segments)
 - Memory: Allocates space for (latitude_segments+1)*(longitude_segments+1)
 vertices and 6*latitude_segments*longitude_segments indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a sphere mesh asset
 auto sphere = MakeSphereMeshAsset(16, 32);
 for (const auto& v : sphere->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
       using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
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
  for (unsigned int lat = 0; lat <= latitude_segments; ++lat) {
    constexpr float pi = std::numbers::pi_v<float>;
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
      float y = cos_theta;
      float z = sin_theta * sin_phi;
      Vertex v {
        .position = { x * 0.5f, y * 0.5f, z * 0.5f },
        .normal = { x, y, z },
        .texcoord
        = { static_cast<float>(lon) / static_cast<float>(longitude_segments),
          1.0f
            - static_cast<float>(lat) / static_cast<float>(latitude_segments) },
        .tangent = { -sin_phi, 0.0f, cos_phi },
        .bitangent = { -cos_theta * cos_phi, sin_theta, -cos_theta * sin_phi },
        .color = { 1, 1, 1, 1 },
      };
      vertices.push_back(v);
    }
  }
  for (unsigned int lat = 0; lat < latitude_segments; ++lat) {
    for (unsigned int lon = 0; lon < longitude_segments; ++lon) {
      uint32_t i0 = lat * (longitude_segments + 1) + lon;
      uint32_t i1 = i0 + longitude_segments + 1;
      uint32_t i2 = i0 + 1;
      uint32_t i3 = i1 + 1;
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
