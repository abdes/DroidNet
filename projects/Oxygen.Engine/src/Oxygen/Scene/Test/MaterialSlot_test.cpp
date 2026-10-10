//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::AssetKey;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::MaterialSlotId;

auto MakeGeometry(
  const AssetKey key, const std::span<const MaterialSlotId> order)
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
        .WithMaterialSlotId(id)
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
  return std::make_shared<GeometryAsset>(key, desc, std::move(meshes));
}

auto Slots() -> std::array<MaterialSlotId, 2>
{
  return {
    MaterialSlotId::FromStableIdentity("slot-test/left"),
    MaterialSlotId::FromStableIdentity("slot-test/right"),
  };
}

NOLINT_TEST(MaterialSlotTest, IndependentInstancesAndNonzeroSlotAcrossLods)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("slots", 8U);
  auto first = scene->CreateNode("first");
  auto second = scene->CreateNode("second");
  const auto ids = Slots();
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
  EXPECT_EQ(geometry->MaterialSlots().slots.size(), 2U);
}

NOLINT_TEST(MaterialSlotTest, ClearRestoresEachLodDefault)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("slots", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Slots();
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

NOLINT_TEST(MaterialSlotTest, ReorderPreservesIdentityInsteadOfOrdinal)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("slots", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Slots();
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

NOLINT_TEST(
  MaterialSlotTest, RemovedSlotAndDifferentGeometryDoNotReuseOverrides)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("slots", 8U);
  auto node = scene->CreateNode("node");
  const auto ids = Slots();
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

} // namespace
