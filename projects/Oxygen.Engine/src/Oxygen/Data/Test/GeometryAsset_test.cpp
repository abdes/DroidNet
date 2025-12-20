//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <memory>
#include <vector>

#include <Oxygen/Core/Constants.h>
// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>

using oxygen::data::GeometryAsset;
using oxygen::data::Mesh;
using oxygen::data::MeshBuilder;
using oxygen::data::Vertex;

namespace {

//! Helper utilities for creating simple geometry assets used by tests.
class GeometryAssetTestHelpers {
public:
  static auto MakeSimpleMesh(std::string name, ::oxygen::Vec3 offset)
    -> std::shared_ptr<Mesh>
  {
    std::vector<Vertex> vertices = {
      {
        .position = offset + ::oxygen::Vec3(0, 0, 0),
        .normal = ::oxygen::Vec3(0, 1, 0),
        .texcoord = ::oxygen::Vec2(0, 0),
        .tangent = ::oxygen::Vec3(1, 0, 0),
        .bitangent = {},
        .color = {},
      },
      {
        .position = offset + ::oxygen::Vec3(1, 0, 0),
        .normal = ::oxygen::Vec3(0, 1, 0),
        .texcoord = ::oxygen::Vec2(1, 0),
        .tangent = ::oxygen::Vec3(1, 0, 0),
        .bitangent = {},
        .color = {},
      },
      {
        .position = offset + ::oxygen::Vec3(0, 1, 0),
        .normal = ::oxygen::Vec3(0, 1, 0),
        .texcoord = ::oxygen::Vec2(0, 1),
        .tangent = ::oxygen::Vec3(1, 0, 0),
        .bitangent = {},
        .color = {},
      },
    };

    std::vector<std::uint32_t> indices = { 0, 1, 2 };
    auto material = oxygen::data::MaterialAsset::CreateDefault();
    return MeshBuilder(0, std::move(name))
      .WithVertices(vertices)
      .WithIndices(indices)
      .BeginSubMesh("main", material)
      .WithMeshView({
        .first_index = 0,
        .index_count = static_cast<uint32_t>(indices.size()),
        .first_vertex = 0,
        .vertex_count = static_cast<uint32_t>(vertices.size()),
      })
      .EndSubMesh()
      .Build();
  }

  static auto MakeGeometryAssetWithTwoLods() -> std::unique_ptr<GeometryAsset>
  {
    auto mesh0 = MakeSimpleMesh("lod0", ::oxygen::Vec3(0, 0, 0));
    auto mesh1 = MakeSimpleMesh("lod1", ::oxygen::Vec3(10, 0, 0));

    oxygen::data::pak::GeometryAssetDesc desc {};
    // Minimal header setup (name left default). Lod count implied by vector
    // size
    desc.lod_count = 2;
    desc.bounding_box_min[0] = 0.0f;
    desc.bounding_box_min[1] = 0.0f;
    desc.bounding_box_min[2] = 0.0f;
    desc.bounding_box_max[0] = 11.0f; // covers second mesh shifted by 10
    desc.bounding_box_max[1] = 1.0f;
    desc.bounding_box_max[2] = 0.0f;

    std::vector<std::shared_ptr<Mesh>> lods = { mesh0, mesh1 };
    return std::make_unique<GeometryAsset>(desc, std::move(lods));
  }
};

//! Verifies Meshes(), MeshAt() for valid indices, and LodCount().
NOLINT_TEST(GeometryAssetBasicTest, LodAccessors_ReturnExpected)
{
  // Arrange
  auto asset = GeometryAssetTestHelpers::MakeGeometryAssetWithTwoLods();

  // Act
  auto lods = asset->Meshes();
  auto lod0 = asset->MeshAt(0);
  auto lod1 = asset->MeshAt(1);
  size_t lod_count = asset->LodCount();

  // Assert
  EXPECT_EQ(lods.size(), 2u);
  EXPECT_EQ(lod_count, 2u);
  EXPECT_NE(lod0, nullptr);
  EXPECT_NE(lod1, nullptr);
  EXPECT_EQ(lod0->VertexCount(), 3u);
  EXPECT_EQ(lod1->VertexCount(), 3u);
}

//! Verifies MeshAt() returns null shared_ptr for out-of-range index.
NOLINT_TEST(GeometryAssetErrorTest, MeshAt_OutOfRange_ReturnsNull)
{
  // Arrange
  auto asset = GeometryAssetTestHelpers::MakeGeometryAssetWithTwoLods();

  // Act
  auto out_of_range = asset->MeshAt(5); // beyond size 2

  // Assert
  EXPECT_EQ(out_of_range, nullptr);
}

//! Verifies bounding box accessors reflect descriptor values exactly.
NOLINT_TEST(GeometryAssetBasicTest, BoundingBoxMatchesDescriptor)
{
  // Arrange
  auto asset = GeometryAssetTestHelpers::MakeGeometryAssetWithTwoLods();

  // Act
  auto min = asset->BoundingBoxMin();
  auto max = asset->BoundingBoxMax();

  // Assert
  EXPECT_EQ(min, ::oxygen::Vec3(0.0f, 0.0f, 0.0f));
  EXPECT_EQ(max, ::oxygen::Vec3(11.0f, 1.0f, 0.0f));
}

} // namespace
