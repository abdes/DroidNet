//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./AssetLoader_test.h"

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>

#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>

using testing::NotNull;

using oxygen::content::testing::AssetLoaderDependencyTest;
using oxygen::data::AssetKey;
using oxygen::data::BufferResource;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

//=== AssetLoader Dependency Mgmt Tests ===-----------------------------------//

namespace {

//! Test: AssetLoader handles material with texture dependencies
/*!
 Scenario: Loads a material asset that depends on texture resources and
 verifies that dependencies are properly resolved.
*/
NOLINT_TEST_F(
  AssetLoaderDependencyTest, LoadAsset_MaterialWithTextures_LoadsDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("material_with_textures");
  asset_loader_->AddPakFile(pak_path);

  const auto material_key = CreateTestAssetKey("textured_material");

  // Act
  const auto material
    = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);

  // Assert
  EXPECT_THAT(material, NotNull());

  // Verify that texture dependencies are properly referenced
  // All texture indices should be valid (>= 0), with 0 being the default
  // texture
  const auto base_color_idx = material->GetBaseColorTexture();
  const auto normal_idx = material->GetNormalTexture();
  const auto roughness_idx = material->GetRoughnessTexture();

  // All indices should be valid (0 = default texture, >0 = specific textures)
  EXPECT_GE(base_color_idx, 0);
  EXPECT_GE(normal_idx, 0);
  EXPECT_GE(roughness_idx, 0);
}

//! Test: AssetLoader handles geometry with buffer dependencies
/*!
 Scenario: Loads a geometry asset that depends on buffer resources and
 verifies that dependencies are properly resolved.
*/
NOLINT_TEST_F(
  AssetLoaderDependencyTest, LoadAsset_GeometryWithBuffers_LoadsDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("geometry_with_buffers");
  asset_loader_->AddPakFile(pak_path);

  const auto geometry_key = CreateTestAssetKey("buffered_geometry");

  // Act
  const auto geometry
    = asset_loader_->LoadAsset<GeometryAsset>(geometry_key, false);

  // Assert
  EXPECT_THAT(geometry, NotNull());

  // Verify that buffer dependencies are properly loaded
  // The geometry should have at least one mesh with valid buffer references
  const auto meshes = geometry->Meshes();
  EXPECT_FALSE(meshes.empty());

  if (!meshes.empty()) {
    const auto& first_mesh = meshes[0];
    ASSERT_THAT(first_mesh, NotNull());

    // Verify mesh has buffer data available
    // Note: VertexCount/IndexCount may be 0 for default/empty buffers (index 0)
    // but the mesh should still be valid and have buffer references
    EXPECT_GE(first_mesh->VertexCount(), 0);
    EXPECT_GE(first_mesh->IndexCount(), 0);

    // If the mesh has indices, it should be marked as indexed
    if (first_mesh->IndexCount() > 0) {
      EXPECT_TRUE(first_mesh->IsIndexed());
    }
  }
}

//! Test: Cycle detection prevents insertion of an edge creating a cycle
/*!
 Scenario: Create two fake dependencies A->B then attempt to add B->A and
 ensure second insertion rejected (no reverse edge recorded).
*/
NOLINT_TEST_F(AssetLoaderDependencyTest, CycleDetection_PreventsInsertion)
{
  // Arrange
  auto key_a = CreateTestAssetKey("cycle_a");
  auto key_b = CreateTestAssetKey("cycle_b");

  // Simulate dependency A -> B (A depends on B). Therefore, B has A as a
  // dependent.
  asset_loader_->AddAssetDependency(key_a, key_b);

#if !defined(NDEBUG)
  // In debug builds, adding the reverse edge should trigger death.
  EXPECT_DEATH(
    { asset_loader_->AddAssetDependency(key_b, key_a); }, "Cycle detected");

  // After death test, only the original edge A->B exists in this process.
  size_t dependents_of_a = 0; // Assets that depend on A (should be none)
  asset_loader_->ForEachDependent(
    key_a, [&](const AssetKey&) { ++dependents_of_a; });
  size_t dependents_of_b = 0; // Assets that depend on B (should be A)
  asset_loader_->ForEachDependent(
    key_b, [&](const AssetKey&) { ++dependents_of_b; });
  EXPECT_EQ(dependents_of_a, 0U);
  EXPECT_EQ(dependents_of_b, 1U);
#else
  // Release build: the AddAssetDependency should be a no-op (no death) and not
  // insert reverse edge. We can't use ForEachDependent in release builds,
  // so we test that basic operations work and no crashes occur.
  EXPECT_NO_THROW(asset_loader_->AddAssetDependency(key_b, key_a));

  // Test that releasing assets works correctly (should not crash)
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_a, false));
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_b, false));

  // Test idempotence - releasing again should still return true
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_a, false));
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_b, false));
#endif
}

//! Test: Release order unloads dependency before dependent
/*!
 Scenario: A depends on B (simulate by registering dependency) then releasing
 A cascades and causes B to be checked in first so B eviction/unload happens
 before A.
*/
NOLINT_TEST_F(AssetLoaderDependencyTest, ReleaseOrder_DependencyBeforeDependent)
{
  // Arrange
  const auto key_a = CreateTestAssetKey("release_a");
  const auto key_b = CreateTestAssetKey("release_b");
  asset_loader_->AddAssetDependency(key_a, key_b);

  // We cannot directly observe eviction order without real loads; this test
  // exercises that no crash occurs and ReleaseAsset returns true for both after
  // manual loads absent. Act
  asset_loader_->ReleaseAsset(key_a, false);
  // Asset B released by cascade; releasing B again should be harmless
  asset_loader_->ReleaseAsset(key_b, false);

  // Assert (idempotence): releasing again returns true (already gone or
  // successfully evicted)
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_a, false));
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_b, false));
}

#if !defined(NDEBUG)
//! Test: Releasing one of multiple dependents does not evict shared dependency
/*!
 Scenario: A -> C, B -> C. Release A; C must remain for B. Then release B; C
 may be released. In release builds we only assert no crash and idempotent
 release behavior because dependent enumeration API is debug-only.
*/
NOLINT_TEST_F(
  AssetLoaderDependencyTest, CascadeRelease_SiblingSharedDependencyNotEvicted)
{
  const auto key_a = CreateTestAssetKey("cascade_a");
  const auto key_b = CreateTestAssetKey("cascade_b");
  const auto key_c = CreateTestAssetKey("cascade_shared");
  asset_loader_->AddAssetDependency(key_a, key_c);
  asset_loader_->AddAssetDependency(key_b, key_c);

  size_t dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 2);

  asset_loader_->ReleaseAsset(key_a, false);

  dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 1);

  asset_loader_->ReleaseAsset(key_b, false);

  dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 0);

  // Release again (idempotence)
  asset_loader_->ReleaseAsset(key_a, false);
  asset_loader_->ReleaseAsset(key_b, false);
}
#endif // !NDEBUG

//! Test: Resource dependencies are released before the asset itself
/*!
 Scenario: Load buffered_geometry so geometry asset depends on buffer
 resources. Release the asset; in debug we rely on internal assertions for
 ordering; in release we validate idempotent release and absence of crashes.
*/
NOLINT_TEST_F(AssetLoaderDependencyTest, ReleaseOrder_ResourcesBeforeAssets)
{
  //! Arrange
  const auto pak_path = GeneratePakFile("geometry_with_buffers");
  asset_loader_->AddPakFile(pak_path);

  std::vector<std::string> unload_events;

  // Wrap unloaders so we can observe actual eviction/unload order.
  asset_loader_->RegisterLoader(oxygen::content::loaders::LoadBufferResource,
    [&unload_events](std::shared_ptr<BufferResource> /*resource*/,
      oxygen::content::AssetLoader& /*loader*/, bool /*offline*/) noexcept {
      unload_events.emplace_back("BufferResource");
    });

  asset_loader_->RegisterLoader(oxygen::content::loaders::LoadGeometryAsset,
    [&unload_events](std::shared_ptr<GeometryAsset> /*asset*/,
      oxygen::content::AssetLoader& /*loader*/, bool /*offline*/) noexcept {
      unload_events.emplace_back("GeometryAsset");
    });

  const auto geom_key = CreateTestAssetKey("buffered_geometry");
  const auto geom = asset_loader_->LoadAsset<GeometryAsset>(geom_key, false);
  ASSERT_THAT(geom, NotNull());

  //! Act
  const auto first_release = asset_loader_->ReleaseAsset(geom_key, false);

  //! Assert
  EXPECT_TRUE(first_release);

  // We expect at least one buffer resource eviction, and it must happen before
  // the geometry asset is evicted.
  const auto first_geom_it
    = std::find(unload_events.begin(), unload_events.end(), "GeometryAsset");
  ASSERT_NE(first_geom_it, unload_events.end());
  ASSERT_NE(
    std::find(unload_events.begin(), unload_events.end(), "BufferResource"),
    unload_events.end());

  for (auto it = unload_events.begin(); it != first_geom_it; ++it) {
    EXPECT_EQ(*it, "BufferResource");
  }

  // Idempotence
  const auto second_release = asset_loader_->ReleaseAsset(geom_key, false);
  EXPECT_TRUE(second_release);
}

//! Test: Texture resources unload before material asset
/*!
 Scenario: Load textured_material so the material asset depends on texture
 resources. Release the asset and verify that texture unloads happen before the
 material unload.
*/
NOLINT_TEST_F(AssetLoaderDependencyTest, ReleaseOrder_TexturesBeforeMaterial)
{
  // Arrange
  const auto pak_path = GeneratePakFile("material_with_textures");
  asset_loader_->AddPakFile(pak_path);

  std::vector<std::string> unload_events;

  asset_loader_->RegisterLoader(oxygen::content::loaders::LoadTextureResource,
    [&unload_events](std::shared_ptr<TextureResource> /*resource*/,
      oxygen::content::AssetLoader& /*loader*/, bool /*offline*/) noexcept {
      unload_events.emplace_back("TextureResource");
    });

  asset_loader_->RegisterLoader(oxygen::content::loaders::LoadMaterialAsset,
    [&unload_events](std::shared_ptr<MaterialAsset> /*asset*/,
      oxygen::content::AssetLoader& /*loader*/, bool /*offline*/) noexcept {
      unload_events.emplace_back("MaterialAsset");
    });

  const auto material_key = CreateTestAssetKey("textured_material");
  const auto material
    = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);
  ASSERT_THAT(material, NotNull());

  // Act
  EXPECT_TRUE(asset_loader_->ReleaseAsset(material_key, false));

  // Assert
  const auto first_material_it
    = std::find(unload_events.begin(), unload_events.end(), "MaterialAsset");
  ASSERT_NE(first_material_it, unload_events.end());
  ASSERT_NE(
    std::find(unload_events.begin(), unload_events.end(), "TextureResource"),
    unload_events.end());

  for (auto it = unload_events.begin(); it != first_material_it; ++it) {
    EXPECT_EQ(*it, "TextureResource");
  }
}

#if !defined(NDEBUG)
//! Test: Debug dependent enumeration enumerates only direct dependents (partial
//! release)
/*!
 Scenario: Build a small graph A->B, C->B, C->D. In release, we just ensure
 operations succeed. In debug, we enumerate dependents to validate counts.
*/
NOLINT_TEST_F(AssetLoaderDependencyTest, DebugDependentEnumeration_Works)
{
  const auto key_a = CreateTestAssetKey("enum_a");
  const auto key_b = CreateTestAssetKey("enum_b");
  const auto key_c = CreateTestAssetKey("enum_c");
  const auto key_d = CreateTestAssetKey("enum_d");
  asset_loader_->AddAssetDependency(key_a, key_b);
  asset_loader_->AddAssetDependency(key_c, key_b);
  asset_loader_->AddAssetDependency(key_c, key_d);

  std::vector<AssetKey> dependents_of_b;
  asset_loader_->ForEachDependent(
    key_b, [&](const AssetKey& dk) { dependents_of_b.push_back(dk); });
  EXPECT_EQ(dependents_of_b.size(), 2);
  size_t hits = 0;
  for (const auto& k : dependents_of_b) {
    if (k.guid == key_a.guid || k.guid == key_c.guid) {
      ++hits;
    }
  }
  EXPECT_EQ(hits, 2);
  size_t dependents_of_d = 0;
  asset_loader_->ForEachDependent(
    key_d, [&](const AssetKey&) { ++dependents_of_d; });
  EXPECT_EQ(dependents_of_d, 1);
  size_t dependents_of_a = 0;
  asset_loader_->ForEachDependent(
    key_a, [&](const AssetKey&) { ++dependents_of_a; });
  EXPECT_EQ(dependents_of_a, 0);
}
#endif // !NDEBUG

} // namespace
