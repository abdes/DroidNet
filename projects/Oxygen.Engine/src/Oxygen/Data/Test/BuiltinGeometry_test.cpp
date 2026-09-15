//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BuiltinGeometry.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace oxygen::data;

//! Every catalog entry resolves a cached, drawable asset using the engine
//! default material.
NOLINT_TEST(
  BuiltinGeometryTest, CatalogResolvesCompleteMeshesAndDefaultMaterial)
{
  ASSERT_EQ(GetBuiltinGeometryNames().size(), 12U);
  for (const auto name : GetBuiltinGeometryNames()) {
    SCOPED_TRACE(name);
    const auto uri
      = "asset:///Engine/Generated/BasicShapes/" + std::string(name);
    const auto identity = ResolveBuiltinGeometryIdentity(uri);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(identity->asset_uri, uri);
    EXPECT_EQ(identity->descriptor_name,
      "Engine_Generated_BasicShapes_" + std::string(name));
    const auto geometry = ResolveBuiltinGeometry(uri);
    ASSERT_NE(geometry, nullptr);
    EXPECT_EQ(ResolveBuiltinGeometry(uri), geometry);
    ASSERT_EQ(geometry->LodCount(), 1U);
    const auto& mesh = geometry->MeshAt(0);
    ASSERT_NE(mesh, nullptr);
    ASSERT_GT(mesh->VertexCount(), 0U);
    ASSERT_GT(mesh->IndexCount(), 0U);
    ASSERT_EQ(mesh->SubMeshes().size(), 1U);
    const auto& submesh = mesh->SubMeshes().front();
    EXPECT_EQ(submesh.Material(), MaterialAsset::CreateDefault());
    ASSERT_EQ(submesh.MeshViews().size(), 1U);
    EXPECT_EQ(submesh.MeshViews().front().VertexCount(), mesh->VertexCount());
    EXPECT_EQ(submesh.MeshViews().front().IndexCount(), mesh->IndexCount());
    EXPECT_EQ(geometry->BoundingBoxMin(), mesh->BoundingBoxMin());
    EXPECT_EQ(geometry->BoundingBoxMax(), mesh->BoundingBoxMax());
  }
}

//! Empty serialized parameters use the exact defaults of the public direct
//! factories.
NOLINT_TEST(
  BuiltinGeometryTest, SerializedGenerationMatchesDirectFactoryDefaults)
{
  using Buffers
    = std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;
  const auto factories
    = std::vector<std::pair<std::string_view, std::function<Buffers()>>> {
        { "Cube", [] { return MakeCubeMeshAsset(); } },
        { "SubdividedCube", [] { return MakeSubdividedCubeMeshAsset(); } },
        { "Sphere", [] { return MakeSphereMeshAsset(); } },
        { "Capsule", [] { return MakeCapsuleMeshAsset(); } },
        { "IcoSphere", [] { return MakeIcoSphereMeshAsset(); } },
        { "GeodesicSphere", [] { return MakeGeodesicSphereMeshAsset(); } },
        { "Plane", [] { return MakePlaneMeshAsset(); } },
        { "Cylinder", [] { return MakeCylinderMeshAsset(); } },
        { "Cone", [] { return MakeConeMeshAsset(); } },
        { "Torus", [] { return MakeTorusMeshAsset(); } },
        { "Quad", [] { return MakeQuadMeshAsset(); } },
        { "ArrowGizmo", [] { return MakeArrowGizmoMeshAsset(); } },
      };
  for (const auto& [name, factory] : factories) {
    SCOPED_TRACE(name);
    const auto direct = factory();
    const auto serialized
      = GenerateMeshBuffers(std::string(name) + "/Mesh", {});
    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(serialized.has_value());
    ASSERT_EQ(serialized->first.size(), direct->first.size());
    EXPECT_EQ(serialized->second, direct->second);
    for (auto index = std::size_t { 0 }; index < direct->first.size();
      ++index) {
      EXPECT_EQ(
        serialized->first[index].position, direct->first[index].position);
      EXPECT_EQ(serialized->first[index].normal, direct->first[index].normal);
      EXPECT_EQ(
        serialized->first[index].texcoord, direct->first[index].texcoord);
      EXPECT_EQ(serialized->first[index].tangent, direct->first[index].tangent);
    }
  }
}

//! Aliases share a recipe while retaining both existing authored URI
//! identities.
NOLINT_TEST(BuiltinGeometryTest, AliasAndThinPlaneSemanticsAreExplicit)
{
  const auto alias = ResolveBuiltinGeometryIdentity(
    "asset:///Engine/Generated/BasicShapes/GeodesicSphere");
  ASSERT_TRUE(alias.has_value());
  EXPECT_EQ(alias->generator, "IcoSphere");
  EXPECT_EQ(alias->name, "GeodesicSphere");
  const auto plane
    = ResolveBuiltinGeometry("asset:///Engine/Generated/BasicShapes/Plane");
  ASSERT_NE(plane, nullptr);
  EXPECT_FLOAT_EQ(plane->BoundingBoxMin().z, 0.0F);
  EXPECT_FLOAT_EQ(plane->BoundingBoxMax().z, 0.0F);
  EXPECT_LT(plane->BoundingBoxMin().y, plane->BoundingBoxMax().y);
  EXPECT_EQ(
    ResolveBuiltinGeometry("asset:///Engine/Generated/BasicShapes/cube"),
    ResolveBuiltinGeometry("asset:///Engine/Generated/BasicShapes/Cube"));
  EXPECT_EQ(
    ResolveBuiltinGeometry("asset:///Content/Models/Cube.ogeo"), nullptr);
  EXPECT_EQ(
    ResolveBuiltinGeometry("asset:///Engine/Generated/BasicShapes/Unknown"),
    nullptr);
}

} // namespace
