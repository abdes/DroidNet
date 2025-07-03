//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <numbers>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>

// ReSharper disable CppClangTidyModernizeUseDesignatedInitializers

namespace {

auto BuildMesh(std::string_view name,
  std::vector<oxygen::data::Vertex> vertices, std::vector<uint32_t> indices)
  -> std::shared_ptr<oxygen::data::Mesh>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::MeshViewDesc;

  auto mesh = MeshBuilder(0, name)
                .WithVertices(std::move(vertices))
                .WithIndices(std::move(indices))
                .BeginSubMesh("default", MaterialAsset::CreateDefault())
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = static_cast<uint32_t>(indices.size()),
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                .Build();

  return mesh;
}

} // namespace

namespace oxygen::data {

/*!
 Creates a new Mesh representing a unit axis-aligned cube centered at the
 origin.

 @return Shared pointer to the immutable Mesh containing the cube geometry.
 Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(1) (fixed-size geometry generation)
 - Memory: Allocates space for 8 vertices and 36 indices
 - Optimization: No dynamic allocations beyond vector growth; all data is
   constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a cube mesh asset
 auto cube = MakeCubeMeshAsset();
 for (const auto& v : cube->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
       using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto MakeCubeMeshAsset() -> std::shared_ptr<Mesh>
{
  // Vertices for a unit cube centered at the origin
  constexpr float h = 0.5f;
  std::vector<Vertex> vertices = {
    // clang-format off
    { { -h, -h, -h }, {  0,  0, -1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 0
    { {  h, -h, -h }, {  0,  0, -1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 1
    { {  h,  h, -h }, {  0,  0, -1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 2
    { { -h,  h, -h }, {  0,  0, -1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 3
    { { -h, -h,  h }, {  0,  0,  1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 4
    { {  h, -h,  h }, {  0,  0,  1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 5
    { {  h,  h,  h }, {  0,  0,  1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 6
    { { -h,  h,  h }, {  0,  0,  1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1, 1 } }, // 7
    // clang-format on
  };
  std::vector<uint32_t> indices = {
    // clang-format off
    // -Z face (back)
    0, 2, 1, 0, 3, 2,
    // +Z face (front)
    4, 5, 6, 4, 6, 7,
    // -X face (left)
    0, 7, 3, 0, 4, 7,
    // +X face (right)
    1, 2, 6, 1, 6, 5,
    // -Y face (bottom)
    0, 1, 5, 0, 5, 4,
    // +Y face (top)
    3, 7, 6, 3, 6, 2,
    // clang-format on
  };

  return BuildMesh("Cube", std::move(vertices), std::move(indices));
}

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
auto MakeSphereMeshAsset(unsigned int latitude_segments,
  unsigned int longitude_segments) -> std::shared_ptr<Mesh>
{
  if (latitude_segments < 3 || longitude_segments < 3) {
    return nullptr;
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

  return BuildMesh("Sphere", std::move(vertices), std::move(indices));
}

/*!
 Creates a new Mesh representing a flat plane in the XZ plane centered at
 the origin. The plane is subdivided into a grid of quads, with each quad made
 of two triangles. Vertices are generated with positions, normals, texcoords,
 tangents, bitangents, and color.

 @param x_segments Number of subdivisions along the X axis (minimum 1).
 @param z_segments Number of subdivisions along the Z axis (minimum 1).
 @param size Length of the plane along X and Z (plane is size x size).
 @return Shared pointer to the immutable Mesh containing the plane
 geometry. Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(x_segments * z_segments)
 - Memory: Allocates space for (x_segments+1)*(z_segments+1) vertices and
 6*x_segments*z_segments indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a 2x2 plane mesh asset of size 1.0
 auto plane = MakePlaneMeshAsset(2, 2, 1.0f);
 for (const auto& v : plane->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
 using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto MakePlaneMeshAsset(unsigned int x_segments, unsigned int z_segments,
  float size) -> std::shared_ptr<Mesh>
{
  if (x_segments < 1 || z_segments < 1 || size <= 0.0f) {
    return nullptr;
  }
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  float half_size = size * 0.5f;
  for (unsigned int z = 0; z <= z_segments; ++z) {
    float z_frac = static_cast<float>(z) / static_cast<float>(z_segments);
    float z_pos = -half_size + z_frac * size;
    for (unsigned int x = 0; x <= x_segments; ++x) {
      float x_frac = static_cast<float>(x) / static_cast<float>(x_segments);
      float x_pos = -half_size + x_frac * size;
      Vertex v {
        .position = { x_pos, 0.0f, z_pos },
        .normal = { 0.0f, 1.0f, 0.0f },
        .texcoord = { x_frac, 1.0f - z_frac },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = { 0.0f, 0.0f, 1.0f },
        .color = { 1, 1, 1, 1 },
      };
      vertices.push_back(v);
    }
  }
  for (unsigned int z = 0; z < z_segments; ++z) {
    for (unsigned int x = 0; x < x_segments; ++x) {
      uint32_t i0 = z * (x_segments + 1) + x;
      uint32_t i1 = i0 + 1;
      uint32_t i2 = i0 + (x_segments + 1);
      uint32_t i3 = i2 + 1;
      indices.push_back(i0);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i3);
    }
  }

  return BuildMesh("Plane", std::move(vertices), std::move(indices));
}

/*!
 Creates a new Mesh representing a cylinder centered at the origin, aligned
 along the Y axis. The cylinder consists of a side surface and two end caps.
 Vertices are generated with positions, normals, texcoords, tangents,
 bitangents, and color.

 @param segments Number of radial segments (minimum 3).
 @param height Height of the cylinder (centered at Y=0).
 @param radius Radius of the cylinder.
 @return Shared pointer to the immutable Mesh containing the cylinder
 geometry. Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(segments)
 - Memory: Allocates space for 2*(segments+1) + 2 vertices and 12*segments
 indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a cylinder mesh asset
auto cylinder = MakeCylinderMeshAsset(32, 1.0f, 0.5f);
for (const auto& v : cylinder->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
 using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto MakeCylinderMeshAsset(const unsigned int segments, const float height,
  const float radius) -> std::shared_ptr<Mesh>
{
  if (segments < 3 || height <= 0.0f || radius <= 0.0f) {
    return nullptr;
  }
  constexpr float pi = std::numbers::pi_v<float>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  float half_height = height * 0.5f;
  // Side vertices
  for (unsigned int i = 0; i <= segments; ++i) {
    float theta
      = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    glm::vec3 normal = { x, 0.0f, z };
    glm::vec3 tangent = { -z, 0.0f, x };
    glm::vec3 bitangent = { 0.0f, 1.0f, 0.0f };
    float u = static_cast<float>(i) / static_cast<float>(segments);
    // Bottom
    vertices.push_back(Vertex {
      .position = { x * radius, -half_height, z * radius },
      .normal = normal,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
    // Top
    vertices.push_back(Vertex {
      .position = { x * radius, half_height, z * radius },
      .normal = normal,
      .texcoord = { u, 0.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
  }
  // Side indices
  for (unsigned int i = 0; i < segments; ++i) {
    uint32_t i0 = i * 2;
    uint32_t i1 = i0 + 1;
    uint32_t i2 = i0 + 2;
    uint32_t i3 = i0 + 3;
    indices.push_back(i0);
    indices.push_back(i2);
    indices.push_back(i1);
    indices.push_back(i1);
    indices.push_back(i2);
    indices.push_back(i3);
  }
  // Center vertices for caps
  uint32_t base_index = static_cast<uint32_t>(vertices.size());
  vertices.push_back(Vertex {
    .position = { 0, -half_height, 0 },
    .normal = { 0, -1, 0 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = { 1, 1, 1, 1 },
  }); // bottom center
  vertices.push_back(Vertex {
    .position = { 0, half_height, 0 },
    .normal = { 0, 1, 0 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = { 1, 1, 1, 1 },
  }); // top center
  // Bottom and top caps
  for (unsigned int i = 0; i < segments; ++i) {
    // Bottom cap
    uint32_t v0 = i * 2;
    uint32_t v1 = ((i + 1) % segments) * 2;
    indices.push_back(base_index + 0);
    indices.push_back(v1);
    indices.push_back(v0);
    // Top cap
    uint32_t v2 = i * 2 + 1;
    uint32_t v3 = ((i + 1) % segments) * 2 + 1;
    indices.push_back(base_index + 1);
    indices.push_back(v2);
    indices.push_back(v3);
  }

  return BuildMesh("Cylinder", std::move(vertices), std::move(indices));
}

/*!
 Creates a new Mesh representing a cone centered at the origin, aligned
 along the Y axis. The cone consists of a side surface and a base cap.
 Vertices are generated with positions, normals, texcoords, tangents,
 bitangents, and color, using designated initializers and trailing commas.

 @param segments Number of radial segments (minimum 3).
 @param height Height of the cone (centered at Y=0, apex at +Y).
 @param radius Base radius of the cone.
 @return Shared pointer to the immutable Mesh containing the cone geometry.
 Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(segments)
 - Memory: Allocates space for (segments+2) vertices and 6*segments indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a cone mesh asset
auto cone = MakeConeMeshAsset(32, 1.0f, 0.5f);
for (const auto& v : cone->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
 using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto MakeConeMeshAsset(unsigned int segments, float height, float radius)
  -> std::shared_ptr<Mesh>
{
  if (segments < 3 || height <= 0.0f || radius <= 0.0f) {
    return nullptr;
  }
  constexpr float pi = std::numbers::pi_v<float>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  float half_height = height * 0.5f;
  glm::vec3 apex = { 0.0f, half_height, 0.0f };
  // Side vertices (base ring)
  for (unsigned int i = 0; i <= segments; ++i) {
    float theta
      = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    float u = static_cast<float>(i) / static_cast<float>(segments);
    glm::vec3 pos = { x * radius, -half_height, z * radius };
    glm::vec3 dir = glm::normalize(glm::vec3(x, radius / height, z));
    glm::vec3 tangent = { -z, 0.0f, x };
    glm::vec3 bitangent = glm::cross(dir, tangent);
    vertices.push_back(Vertex {
      .position = pos,
      .normal = dir,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = { 1, 1, 1, 1 },
    });
  }
  // Apex vertex
  vertices.push_back(Vertex {
    .position = apex,
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.0f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = { 1, 1, 1, 1 },
  });
  uint32_t apex_index = static_cast<uint32_t>(vertices.size() - 1);
  // Side indices
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(apex_index);
    indices.push_back(i);
    indices.push_back(i + 1);
  }
  // Center vertex for base cap
  vertices.push_back(Vertex {
    .position = { 0, -half_height, 0 },
    .normal = { 0, -1, 0 },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = { 1, 1, 1, 1 },
  });
  uint32_t base_center = static_cast<uint32_t>(vertices.size() - 1);
  // Base cap indices (CCW: i, i+1, center)
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(i);
    indices.push_back(i + 1);
    indices.push_back(base_center);
  }

  return BuildMesh("Cone", std::move(vertices), std::move(indices));
}

/*!
 Creates a new Mesh representing a torus (doughnut shape) centered at the
 origin, aligned along the Y axis. The torus is generated by sweeping a circle
 (minor radius) around a larger circle (major radius). Vertices are generated
 with positions, normals, texcoords, tangents, bitangents, and color.

 @param major_segments Number of segments around the main ring (minimum 3).
 @param minor_segments Number of segments around the tube (minimum 3).
 @param major_radius Radius from the center to the center of the tube.
 @param minor_radius Radius of the tube.
 @return Shared pointer to the immutable Mesh containing the torus
 geometry. Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(major_segments * minor_segments)
 - Memory: Allocates space for (major_segments+1)*(minor_segments+1) vertices
 and 6*major_segments*minor_segments indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a torus mesh asset
auto torus = MakeTorusMeshAsset(32, 16, 1.0f, 0.25f);
for (const auto& v : torus->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
       using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto MakeTorusMeshAsset(unsigned int major_segments,
  unsigned int minor_segments, float major_radius, float minor_radius)
  -> std::shared_ptr<Mesh>
{
  if (major_segments < 3 || minor_segments < 3 || major_radius <= 0.0f
    || minor_radius <= 0.0f) {
    return nullptr;
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
      = { major_radius * cos_major, 0.0f, major_radius * sin_major };
    for (unsigned int j = 0; j <= minor_segments; ++j) {
      float minor_theta = 2.0f * pi * static_cast<float>(j)
        / static_cast<float>(minor_segments);
      float cos_minor = std::cos(minor_theta);
      float sin_minor = std::sin(minor_theta);
      glm::vec3 normal
        = { cos_major * cos_minor, sin_minor, sin_major * cos_minor };
      glm::vec3 pos = major_center + normal * minor_radius;
      glm::vec2 texcoord
        = { static_cast<float>(i) / static_cast<float>(major_segments),
            static_cast<float>(j) / static_cast<float>(minor_segments) };
      glm::vec3 tangent
        = { -sin_major * cos_minor, 0.0f, cos_major * cos_minor };
      glm::vec3 bitangent = glm::cross(normal, tangent);
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
      uint32_t i1 = ((i + 1) % (major_segments + 1)) * (minor_segments + 1) + j;
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

  return BuildMesh("Torus", std::move(vertices), std::move(indices));
}

/*!
 Creates a new Mesh representing a quad (rectangle) in the XZ plane,
 centered at the origin. The quad is made of two triangles. Vertices are
 generated with positions, normals, texcoords, tangents, bitangents, and color.

 @param width Width of the quad along the X axis (must be > 0).
 @param height Height of the quad along the Z axis (must be > 0).
 @return Shared pointer to the immutable Mesh containing the quad geometry.
 Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: Allocates space for 4 vertices and 6 indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create a quad mesh asset
auto quad = MakeQuadMeshAsset(2.0f, 1.0f);
for (const auto& v : quad->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
       using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto MakeQuadMeshAsset(const float width, const float height)
  -> std::shared_ptr<Mesh>
{
  if (width <= 0.0f || height <= 0.0f) {
    return nullptr;
  }
  float half_w = width * 0.5f;
  float half_h = height * 0.5f;
  std::vector<Vertex> vertices = {
    // clang-format off
    { { -half_w, 0.0f, -half_h }, { 0, 1, 0 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1, 1 } },
    { {  half_w, 0.0f, -half_h }, { 0, 1, 0 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1, 1 } },
    { {  half_w, 0.0f,  half_h }, { 0, 1, 0 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1, 1 } },
    { { -half_w, 0.0f,  half_h }, { 0, 1, 0 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1, 1 } },
    // clang-format on
  };
  std::vector<uint32_t> indices = {
    0, 1, 2, // triangle 1
    2, 3, 0, // triangle 2
  };

  return BuildMesh("Quad", std::move(vertices), std::move(indices));
}

/*!
 Creates a new Mesh representing a simple arrow gizmo, typically used for
 axis visualization in editors and debug views. The arrow is aligned along the
 +Y axis, composed of a cylinder shaft and a cone head, with distinct colors
 for shaft and head. All geometry is centered at the origin.

 @return Shared pointer to the immutable Mesh containing the arrow gizmo.
 Returns nullptr on invalid input. Never throws.

 ### Performance Characteristics

 - Time Complexity: O(segments)
 - Memory: Allocates space for a small number of vertices and indices
 - Optimization: All data is constructed in-place and moved into the Mesh.

 ### Usage Examples

 ```cpp
// Create an arrow gizmo mesh asset
auto arrow = MakeArrowGizmoMeshAsset();
for (const auto& v : arrow->Vertices()) { ... }
 ```

 @note The default view covers the entire mesh. Submesh views can be created
       using Mesh::MakeView.
 @see Mesh, MeshView, Vertex
*/
auto MakeArrowGizmoMeshAsset() -> std::shared_ptr<Mesh>
{
  constexpr unsigned int segments = 16;
  constexpr float shaft_length = 0.7f;
  constexpr float head_length = 0.18f;
  constexpr float base_y = -0.1f;
  constexpr float shaft_top_y = base_y + shaft_length;
  constexpr float head_top_y = shaft_top_y + head_length;
  constexpr glm::vec4 shaft_color = { 0.2f, 0.6f, 1.0f, 1.0f }; // blueish
  constexpr glm::vec4 head_color = { 1.0f, 0.8f, 0.2f, 1.0f }; // yellowish
  constexpr float pi = std::numbers::pi_v<float>;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // Shaft (cylinder)
  for (unsigned int i = 0; i <= segments; ++i) {
    constexpr float shaft_radius = 0.025f;
    float theta
      = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    glm::vec3 normal = { x, 0.0f, z };
    glm::vec3 tangent = { -z, 0.0f, x };
    glm::vec3 bitangent = { 0.0f, 1.0f, 0.0f };
    float u = static_cast<float>(i) / static_cast<float>(segments);
    // Bottom ring
    vertices.push_back(Vertex {
      .position = { x * shaft_radius, base_y, z * shaft_radius },
      .normal = normal,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = shaft_color,
    });
    // Top ring
    vertices.push_back(Vertex {
      .position = { x * shaft_radius, shaft_top_y, z * shaft_radius },
      .normal = normal,
      .texcoord = { u, 0.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = shaft_color,
    });
  }
  // Shaft indices (side quads)
  for (unsigned int i = 0; i < segments; ++i) {
    uint32_t i0 = i * 2;
    uint32_t i1 = i0 + 1;
    uint32_t i2 = i0 + 2;
    uint32_t i3 = i0 + 3;
    indices.push_back(i0);
    indices.push_back(i2);
    indices.push_back(i1);
    indices.push_back(i1);
    indices.push_back(i2);
    indices.push_back(i3);
  }
  // Head (cone)
  uint32_t cone_base_start = static_cast<uint32_t>(vertices.size());
  for (unsigned int i = 0; i <= segments; ++i) {
    constexpr float head_radius = 0.06f;
    float theta
      = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(theta);
    float z = std::sin(theta);
    float u = static_cast<float>(i) / static_cast<float>(segments);
    glm::vec3 pos = { x * head_radius, shaft_top_y, z * head_radius };
    glm::vec3 dir = glm::normalize(glm::vec3(x, head_radius / head_length, z));
    glm::vec3 tangent = { -z, 0.0f, x };
    glm::vec3 bitangent = glm::cross(dir, tangent);
    vertices.push_back(Vertex {
      .position = pos,
      .normal = dir,
      .texcoord = { u, 1.0f },
      .tangent = tangent,
      .bitangent = bitangent,
      .color = head_color,
    });
  }
  // Cone apex
  vertices.push_back(Vertex {
    .position = { 0.0f, head_top_y, 0.0f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.0f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = head_color,
  });
  uint32_t apex_index = static_cast<uint32_t>(vertices.size() - 1);
  // Cone side indices
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(apex_index);
    indices.push_back(cone_base_start + i);
    indices.push_back(cone_base_start + i + 1);
  }
  // Cone base center
  vertices.push_back(Vertex {
    .position = { 0.0f, shaft_top_y, 0.0f },
    .normal = { 0.0f, -1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 0, 1 },
    .color = head_color,
  });
  uint32_t base_center = static_cast<uint32_t>(vertices.size() - 1);
  // Cone base cap indices (CCW: i, i+1, center)
  for (unsigned int i = 0; i < segments; ++i) {
    indices.push_back(cone_base_start + i);
    indices.push_back(cone_base_start + i + 1);
    indices.push_back(base_center);
  }

  return BuildMesh("ArrowGizmo", std::move(vertices), std::move(indices));
}

} // namespace oxygen::data
