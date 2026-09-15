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
  ASSERT_EQ(GetBuiltinGeometryNames().size(), 11U);
  for (const auto name : GetBuiltinGeometryNames()) {
    SCOPED_TRACE(name);
    const auto uri
      = "asset:///Engine/Generated/BasicShapes/" + std::string(name);
    const auto identity = ResolveBuiltinGeometryIdentity(uri);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(identity->asset_uri, uri);
    EXPECT_EQ(identity->generator, name);
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

//! The native catalog owns which built-ins are authoring choices and which
//! remain available only to internal tools.
NOLINT_TEST(
  BuiltinGeometryTest, AuthoringCategoriesSeparateToolsAndAdvancedShapes)
{
  auto standard_count = 0U;
  auto advanced_count = 0U;
  auto internal_count = 0U;
  for (const auto name : GetBuiltinGeometryNames()) {
    const auto identity = ResolveBuiltinGeometryIdentity(
      "asset:///Engine/Generated/BasicShapes/" + std::string(name));
    ASSERT_TRUE(identity.has_value());
    switch (identity->authoring_category) {
    case BuiltinGeometryAuthoringCategory::kStandard:
      ++standard_count;
      break;
    case BuiltinGeometryAuthoringCategory::kAdvanced:
      ++advanced_count;
      EXPECT_EQ(name, "SubdividedCube");
      break;
    case BuiltinGeometryAuthoringCategory::kInternal:
      ++internal_count;
      EXPECT_EQ(name, "ArrowGizmo");
      break;
    }
  }
  EXPECT_EQ(standard_count, 9U);
  EXPECT_EQ(advanced_count, 1U);
  EXPECT_EQ(internal_count, 1U);
}

//! Canonical resolution is case-insensitive; thin planes keep exact bounds.
NOLINT_TEST(
  BuiltinGeometryTest, CanonicalIdentityAndThinPlaneSemanticsAreExplicit)
{
  const auto sphere = ResolveBuiltinGeometryIdentity(
    "asset:///Engine/Generated/BasicShapes/icosphere");
  ASSERT_TRUE(sphere.has_value());
  EXPECT_EQ(sphere->generator, "IcoSphere");
  EXPECT_EQ(sphere->name, "IcoSphere");
  EXPECT_EQ(
    sphere->asset_uri, "asset:///Engine/Generated/BasicShapes/IcoSphere");
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
