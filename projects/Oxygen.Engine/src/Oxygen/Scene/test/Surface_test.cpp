//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::AssetKey;
using oxygen::data::GeometryAsset;
using oxygen::data::LodIndex;
using oxygen::data::MaterialAsset;
using oxygen::data::SubmeshIndex;
using oxygen::data::SurfaceId;
using oxygen::data::pak::geometry::LodPolicyRecord;
using oxygen::data::pak::geometry::LodPolicyType;
using oxygen::data::pak::geometry::SurfaceDesc;

//! A two-LOD geometry binding one submesh per LOD to each surface of `order`.
/*!
 With `surface_table` the geometry declares those surface defaults and
 `lod_policy`; without it, it declares none, as one built at runtime.
*/
auto MakeGeometry(const AssetKey key, const std::span<const SurfaceId> order,
  const std::optional<std::span<const SurfaceDesc>> surface_table
  = std::nullopt,
  const LodPolicyRecord& lod_policy = {})
  -> std::shared_ptr<const GeometryAsset>
{
  std::vector<std::shared_ptr<oxygen::data::Mesh>> meshes;
  constexpr uint32_t kLodCount = 2U;
  constexpr uint32_t kTriangleSize = 3U;
  for (uint32_t lod = 0; lod < kLodCount; ++lod) {
    std::vector<oxygen::data::Vertex> vertices(kTriangleSize);
    vertices.at(0).position = { 0.0F, 0.0F, 0.0F };
    vertices.at(1).position = { 1.0F, 0.0F, 0.0F };
    vertices.at(2).position = { 0.0F, 1.0F, 0.0F };
    oxygen::data::MeshBuilder builder(
      oxygen::data::LodIndex { lod }, "same-name");
    constexpr auto kIndices
      = std::array<uint32_t, kTriangleSize> { 0U, 1U, 2U };
    builder.WithVertices(std::move(vertices)).WithIndices(kIndices);
    for (const auto id : order) {
      builder
        .BeginSubMesh("duplicate-label",
          lod == 0U ? MaterialAsset::CreateDefault()
                    : MaterialAsset::CreateDebug())
        .WithSurfaceId(id)
        .WithMeshView({
          .first_index = 0U,
          .index_count = kTriangleSize,
          .first_vertex = 0U,
          .vertex_count = kTriangleSize,
        })
        .EndSubMesh();
    }
    meshes.push_back(builder.Build());
  }
  oxygen::data::pak::geometry::GeometryAssetDesc desc {};
  desc.header.version = oxygen::data::pak::geometry::kGeometryAssetVersion;
  desc.lod_count = kLodCount;
  desc.default_lod_policy = lod_policy;
  return std::make_shared<GeometryAsset>(
    key, desc, std::move(meshes), oxygen::data::SourceOrigin {}, surface_table);
}

auto Surfaces() -> std::array<SurfaceId, 2>
{
  return {
    SurfaceId::FromStableIdentity("surface-test/left"),
    SurfaceId::FromStableIdentity("surface-test/right"),
  };
}

NOLINT_TEST(SurfaceTest, IndependentInstancesAndNonzeroSurfaceAcrossLods)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("surfaces", 8U);
  auto first = scene->CreateNode("first");
  auto second = scene->CreateNode("second");
  const auto ids = Surfaces();
  const auto geometry
    = MakeGeometry(AssetKey::FromVirtualPath("/mesh.ogeo"), ids);
  first.GetRenderable().SetGeometry(geometry);
  second.GetRenderable().SetGeometry(geometry);
  const auto original = second.GetRenderable().ResolveSubmeshMaterial(
    oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 1U });
  const auto replacement = MaterialAsset::CreateDebug();
  ASSERT_TRUE(
    first.GetRenderable().SetMaterialOverride(ids.at(1), replacement));
  EXPECT_EQ(first.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 1U }),
    replacement);
  EXPECT_EQ(first.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 1U }, oxygen::data::SubmeshIndex { 1U }),
    replacement);
  EXPECT_EQ(first.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 0U }),
    original);
  EXPECT_EQ(second.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 1U }),
    original);
  EXPECT_EQ(geometry->Surfaces().surfaces.size(), 2U);
}

NOLINT_TEST(SurfaceTest, ClearRestoresEachLodDefault)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("surfaces", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Surfaces();
  node.GetRenderable().SetGeometry(
    MakeGeometry(AssetKey::FromVirtualPath("/mesh.ogeo"), ids));
  const auto lod0 = node.GetRenderable().ResolveSubmeshMaterial(
    oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 1U });
  const auto lod1 = node.GetRenderable().ResolveSubmeshMaterial(
    oxygen::data::LodIndex { 1U }, oxygen::data::SubmeshIndex { 1U });
  ASSERT_TRUE(node.GetRenderable().SetMaterialOverride(ids.at(1), lod1));
  ASSERT_TRUE(node.GetRenderable().ClearMaterialOverride(ids.at(1)));
  EXPECT_EQ(node.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 1U }),
    lod0);
  EXPECT_EQ(node.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 1U }, oxygen::data::SubmeshIndex { 1U }),
    lod1);
}

NOLINT_TEST(SurfaceTest, ReorderPreservesIdentityInsteadOfOrdinal)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("surfaces", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Surfaces();
  const auto key = AssetKey::FromVirtualPath("/mesh.ogeo");
  node.GetRenderable().SetGeometry(MakeGeometry(key, ids));
  const auto replacement = MaterialAsset::CreateDebug();
  ASSERT_TRUE(node.GetRenderable().SetMaterialOverride(ids.at(1), replacement));
  const std::array reordered { ids.at(1), ids.at(0) };
  node.GetRenderable().SetGeometry(MakeGeometry(key, reordered));
  EXPECT_EQ(node.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 0U }),
    replacement);
  EXPECT_EQ(node.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 1U }),
    MaterialAsset::CreateDefault());
}

NOLINT_TEST(SurfaceTest, RemovedSurfaceAndDifferentGeometryDoNotReuseOverrides)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("surfaces", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Surfaces();
  const auto key = AssetKey::FromVirtualPath("/mesh.ogeo");
  node.GetRenderable().SetGeometry(MakeGeometry(key, ids));
  ASSERT_TRUE(node.GetRenderable().SetMaterialOverride(
    ids.at(1), MaterialAsset::CreateDebug()));
  const std::array remaining { ids.at(0) };
  node.GetRenderable().SetGeometry(MakeGeometry(key, remaining));
  EXPECT_FALSE(node.GetRenderable().SetMaterialOverride(
    ids.at(1), MaterialAsset::CreateDebug()));
  node.GetRenderable().SetGeometry(
    MakeGeometry(AssetKey::FromVirtualPath("/other.ogeo"), ids));
  EXPECT_EQ(node.GetRenderable().ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 1U }),
    MaterialAsset::CreateDefault());
}

//! Surface defaults apply without overrides; a runtime geometry takes the
//! schema's.
NOLINT_TEST(SurfaceTest, VisibilityAndShadowsTakeTheAssetDefaults)
{
  using oxygen::data::pak::geometry::kSurfaceFlag_CastsShadows;
  using oxygen::data::pak::geometry::kSurfaceFlag_Visible;

  auto scene = std::make_shared<oxygen::scene::Scene>("surfaces", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Surfaces();
  const auto table = std::array {
    SurfaceDesc { .surface_id = ids.at(0), .flags = kSurfaceFlag_Visible },
    SurfaceDesc { .surface_id = ids.at(1),
      .flags = kSurfaceFlag_Visible | kSurfaceFlag_CastsShadows },
  };
  auto renderable = node.GetRenderable();
  renderable.SetGeometry(MakeGeometry(
    AssetKey::FromVirtualPath("/mesh.ogeo"), ids, std::span { table }));
  EXPECT_TRUE(
    renderable.IsSubmeshVisible(LodIndex { 1U }, SubmeshIndex { 0U }));
  EXPECT_FALSE(
    renderable.SubmeshCastsShadows(LodIndex { 1U }, SubmeshIndex { 0U }));
  EXPECT_TRUE(
    renderable.SubmeshCastsShadows(LodIndex { 1U }, SubmeshIndex { 1U }));

  renderable.SetGeometry(
    MakeGeometry(AssetKey::FromVirtualPath("/runtime.ogeo"), ids));
  EXPECT_TRUE(
    renderable.IsSubmeshVisible(LodIndex { 0U }, SubmeshIndex { 0U }));
  EXPECT_TRUE(
    renderable.SubmeshCastsShadows(LodIndex { 0U }, SubmeshIndex { 0U }));
}

//! Overrides apply to every binding of the surface, and a hidden surface casts
//! no shadow whatever its CastsShadows says.
NOLINT_TEST(SurfaceTest, VisibilityAndShadowOverridesResolvePerSurface)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("surfaces", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Surfaces();
  auto renderable = node.GetRenderable();
  renderable.SetGeometry(
    MakeGeometry(AssetKey::FromVirtualPath("/mesh.ogeo"), ids));

  ASSERT_TRUE(renderable.SetCastsShadowsOverride(ids.at(1), false));
  EXPECT_TRUE(
    renderable.IsSubmeshVisible(LodIndex { 0U }, SubmeshIndex { 1U }));
  EXPECT_FALSE(
    renderable.SubmeshCastsShadows(LodIndex { 0U }, SubmeshIndex { 1U }));
  EXPECT_FALSE(
    renderable.SubmeshCastsShadows(LodIndex { 1U }, SubmeshIndex { 1U }));
  EXPECT_TRUE(
    renderable.SubmeshCastsShadows(LodIndex { 0U }, SubmeshIndex { 0U }));

  ASSERT_TRUE(renderable.ClearCastsShadowsOverride(ids.at(1)));
  ASSERT_TRUE(renderable.SetVisibilityOverride(ids.at(1), false));
  EXPECT_FALSE(
    renderable.IsSubmeshVisible(LodIndex { 1U }, SubmeshIndex { 1U }));
  EXPECT_FALSE(
    renderable.SubmeshCastsShadows(LodIndex { 1U }, SubmeshIndex { 1U }));

  ASSERT_TRUE(renderable.ClearVisibilityOverride(ids.at(1)));
  EXPECT_TRUE(
    renderable.IsSubmeshVisible(LodIndex { 1U }, SubmeshIndex { 1U }));
  EXPECT_TRUE(
    renderable.SubmeshCastsShadows(LodIndex { 1U }, SubmeshIndex { 1U }));
  EXPECT_FALSE(renderable.SetVisibilityOverride(
    SurfaceId::FromStableIdentity("surface-test/unknown"), false));
}

//! Replacement with the same geometry keeps the overrides of the surfaces it
//! still declares.
NOLINT_TEST(SurfaceTest, ReplacementKeepsVisibilityOverridesBySurface)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("surfaces", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Surfaces();
  const auto key = AssetKey::FromVirtualPath("/mesh.ogeo");
  auto renderable = node.GetRenderable();
  renderable.SetGeometry(MakeGeometry(key, ids));
  ASSERT_TRUE(renderable.SetVisibilityOverride(ids.at(1), false));
  const std::array reordered { ids.at(1), ids.at(0) };
  renderable.SetGeometry(MakeGeometry(key, reordered));
  EXPECT_FALSE(
    renderable.IsSubmeshVisible(LodIndex { 0U }, SubmeshIndex { 0U }));
  EXPECT_TRUE(
    renderable.IsSubmeshVisible(LodIndex { 0U }, SubmeshIndex { 1U }));
}

//! The LOD policy is the override, else the geometry's default, else the
//! schema's fixed finest LOD; the override survives replacement.
NOLINT_TEST(SurfaceTest, LodPolicyOverrideReplacesTheGeometryDefault)
{
  using oxygen::scene::DistancePolicy;
  using oxygen::scene::FixedPolicy;

  auto scene = std::make_shared<oxygen::scene::Scene>("surfaces", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Surfaces();
  const auto table = std::array {
    SurfaceDesc { .surface_id = ids.at(0) },
    SurfaceDesc { .surface_id = ids.at(1) },
  };
  const auto policy = LodPolicyRecord {
    .type = LodPolicyType::kDistance,
    .boundary_count = 1,
    .thresholds = { 2.0F },
    .hysteresis_ratio = 0.25F,
  };
  auto renderable = node.GetRenderable();
  renderable.SetGeometry(MakeGeometry(
    AssetKey::FromVirtualPath("/mesh.ogeo"), ids, std::span { table }, policy));
  ASSERT_TRUE(renderable.UsesDistancePolicy());
  const auto distance
    = std::get<DistancePolicy>(renderable.GetLodPolicy().value());
  EXPECT_THAT(distance.thresholds, ::testing::ElementsAre(2.0F));
  EXPECT_FLOAT_EQ(distance.hysteresis_ratio, 0.25F);
  EXPECT_FALSE(renderable.HasLodPolicyOverride());

  renderable.SetLodPolicyOverride(FixedPolicy { .index = LodIndex { 1U } });
  EXPECT_TRUE(renderable.HasLodPolicyOverride());
  EXPECT_EQ(renderable.GetActiveLodIndex(), LodIndex { 1U });

  renderable.SetGeometry(
    MakeGeometry(AssetKey::FromVirtualPath("/runtime.ogeo"), ids));
  EXPECT_EQ(renderable.GetActiveLodIndex(), LodIndex { 1U });

  renderable.ClearLodPolicyOverride();
  EXPECT_TRUE(renderable.UsesFixedPolicy());
  EXPECT_EQ(renderable.GetActiveLodIndex(), LodIndex { 0U });
}

} // namespace
