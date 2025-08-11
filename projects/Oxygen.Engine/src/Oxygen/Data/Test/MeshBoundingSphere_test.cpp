//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/Vertex.h>

using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::data::MeshBuilder;
using oxygen::data::Vertex;

namespace {
//! Verifies that Mesh::BoundingSphere encloses all vertices (owned storage).
class MeshBoundingSphereOwnedTest : public testing::Test { };

NOLINT_TEST_F(MeshBoundingSphereOwnedTest, ComputedSphereContainsAllVertices)
{
  // Arrange
  std::vector<Vertex> vertices {
    { .position = { -1.0f, -2.0f, 0.5f },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} },
    { .position = { 3.0f, 1.0f, -0.5f },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} },
    { .position = { 0.0f, 4.0f, 2.0f },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} },
    { .position = { -2.0f, 0.0f, -3.0f },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} },
  };
  std::vector<std::uint32_t> indices { 0, 1, 2, 2, 3, 0 };

  auto material = MaterialAsset::CreateDefault();
  MeshBuilder builder;
  builder.WithVertices(vertices)
    .WithIndices(indices)
    .BeginSubMesh("owned", material)
    .WithMeshView({ .first_index = 0,
      .index_count = static_cast<uint32_t>(indices.size()),
      .first_vertex = 0,
      .vertex_count = static_cast<uint32_t>(vertices.size()) })
    .EndSubMesh();
  auto mesh = builder.Build();

  const auto sphere = mesh->BoundingSphere(); // center.xyz, radius.w
  const glm::vec3 center { sphere.x, sphere.y, sphere.z };
  const float radius = sphere.w;

  // Act & Assert
  for (const auto& v : vertices) {
    const float dist = glm::length(v.position - center);
    EXPECT_LE(dist, radius + 1e-4f)
      << "Vertex must lie within (or on) computed bounding sphere";
  }
}

//! Verifies that Mesh::BoundingSphere encloses all vertices (referenced
//! storage).
class MeshBoundingSphereReferencedTest : public testing::Test { };

NOLINT_TEST_F(
  MeshBoundingSphereReferencedTest, ComputedSphereContainsAllVertices)
{
  // Arrange: reuse geometry from owned test to ensure identical bounds
  std::vector<Vertex> vertices {
    { .position = { -1.0f, -2.0f, 0.5f },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} },
    { .position = { 3.0f, 1.0f, -0.5f },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} },
    { .position = { 0.0f, 4.0f, 2.0f },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} },
    { .position = { -2.0f, 0.0f, -3.0f },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} },
  };
  std::vector<std::uint32_t> indices { 0, 1, 2, 2, 3, 0 };

  // Build temporary owned mesh to obtain a vertex/index buffer snapshot
  MeshBuilder temp_builder;
  temp_builder.WithVertices(vertices)
    .WithIndices(indices)
    .BeginSubMesh("tmp", MaterialAsset::CreateDefault())
    .WithMeshView({ .first_index = 0,
      .index_count = static_cast<uint32_t>(indices.size()),
      .first_vertex = 0,
      .vertex_count = static_cast<uint32_t>(vertices.size()) })
    .EndSubMesh();
  auto temp_mesh = temp_builder.Build();

  // Create buffer resources from temporary mesh raw data
  const auto vb_view = temp_mesh->Vertices();
  const auto ib_view = temp_mesh->IndexBuffer().AsU32();

  oxygen::data::pak::BufferResourceDesc vdesc { .data_offset = 0,
    .size_bytes = static_cast<oxygen::data::pak::DataBlobSizeT>(
      vb_view.size() * sizeof(Vertex)),
    .usage_flags = static_cast<uint32_t>(
      oxygen::data::BufferResource::UsageFlags::kVertexBuffer),
    .element_stride = sizeof(Vertex),
    .element_format = 0,
    .reserved = {} };
  std::vector<uint8_t> vbytes(vb_view.size() * sizeof(Vertex));
  std::memcpy(vbytes.data(), vb_view.data(), vbytes.size());

  oxygen::data::pak::BufferResourceDesc idesc { .data_offset = 0,
    .size_bytes = static_cast<oxygen::data::pak::DataBlobSizeT>(
      ib_view.size() * sizeof(uint32_t)),
    .usage_flags = static_cast<uint32_t>(
      oxygen::data::BufferResource::UsageFlags::kIndexBuffer),
    .element_stride = sizeof(uint32_t),
    .element_format = 0,
    .reserved = {} };
  std::vector<uint8_t> ibytes(ib_view.size() * sizeof(uint32_t));
  std::memcpy(ibytes.data(), ib_view.data(), ibytes.size());

  auto vertex_buffer = std::make_shared<oxygen::data::BufferResource>(
    std::move(vdesc), std::move(vbytes));
  auto index_buffer = std::make_shared<oxygen::data::BufferResource>(
    std::move(idesc), std::move(ibytes));

  auto material = MaterialAsset::CreateDefault();
  MeshBuilder builder;
  builder.WithBufferResources(vertex_buffer, index_buffer)
    .BeginSubMesh("ref", material)
    .WithMeshView({ .first_index = 0,
      .index_count = static_cast<uint32_t>(indices.size()),
      .first_vertex = 0,
      .vertex_count = static_cast<uint32_t>(vertices.size()) })
    .EndSubMesh();
  auto mesh = builder.Build();

  const auto sphere = mesh->BoundingSphere();
  const glm::vec3 center { sphere.x, sphere.y, sphere.z };
  const float radius = sphere.w;

  // Act & Assert
  for (const auto& v : vertices) {
    const float dist = glm::length(v.position - center);
    EXPECT_LE(dist, radius + 1e-4f)
      << "Vertex must lie within (or on) computed bounding sphere";
  }
}

} // namespace
