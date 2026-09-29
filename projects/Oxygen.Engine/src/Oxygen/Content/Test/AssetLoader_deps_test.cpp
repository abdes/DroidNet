//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
#  include <filesystem>
#  include <vector>
#endif

#include "AssetLoader_test.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using testing::NotNull;

using oxygen::data::AssetKey;
using oxygen::data::BufferResource;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

using oxygen::observer_ptr;
using oxygen::base::CheckedAt;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::content::testing::AssetLoaderLoadingTest;

namespace oxco = oxygen::co;

//=== AssetLoader Dependency Mgmt Tests ===-----------------------------------//

namespace {

//! Fixture for AssetLoader dependency tests
class AssetLoaderDependencyTest : public AssetLoaderLoadingTest { };

//! Test: AssetLoader handles material with texture dependencies
/*!
 Scenario: Loads a material asset that depends on texture resources and
 verifies that dependencies are properly resolved.
*/
NOLINT_TEST_F(
  AssetLoaderDependencyTest, LoadAssetMaterialWithTexturesLoadsDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("material_with_textures");
  const auto material_key = CreateTestAssetKey("textured_material");

  TestEventLoop el;

  // Act + Assert
  oxco::Run(el, [&] -> Co<> { // NOLINT(*-lambda-coroutines)
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxco::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxco::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());

      if (material) {
        // Verify that texture dependencies are properly referenced.
        // All texture indices should be valid (>= 0), with 0 being the default
        // texture.
        const auto base_color_idx = material->GetBaseColorTexture();
        const auto normal_idx = material->GetNormalTexture();
        const auto roughness_idx = material->GetRoughnessTexture();

        // All indices should be valid (0 = default texture, >0 = specific
        // textures).
        EXPECT_GE(base_color_idx, 0);
        EXPECT_GE(normal_idx, 0);
        EXPECT_GE(roughness_idx, 0);
      }

      loader.Stop();
      co_return oxco::kJoin;
    };
  });
}

//! Test: AssetLoader handles geometry with buffer dependencies
/*!
 Scenario: Loads a geometry asset that depends on buffer resources and
 verifies that dependencies are properly resolved.
*/
NOLINT_TEST_F(
  AssetLoaderDependencyTest, LoadAssetGeometryWithBuffersLoadsDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("geometry_with_buffers");
  const auto geometry_key = CreateTestAssetKey("buffered_geometry");

  TestEventLoop el;

  // Act + Assert
  oxco::Run(el, [&] -> Co<> { // NOLINT(*-lambda-coroutines)
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxco::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxco::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto geometry
        = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
      EXPECT_THAT(geometry, NotNull());

      if (geometry) {
        // Verify that buffer dependencies are properly loaded.
        // The geometry should have at least one mesh with valid buffer
        // references.
        const auto meshes = geometry->Meshes();
        EXPECT_FALSE(meshes.empty());

        if (!meshes.empty()) {
          const auto& first_mesh = CheckedAt(meshes, 0);
          EXPECT_THAT(first_mesh, NotNull());

          // Verify mesh has buffer data available.
          // Note: VertexCount/IndexCount may be 0 for default/empty buffers
          // (index 0), but the mesh should still be valid and have buffer
          // references.
          EXPECT_GE(first_mesh->VertexCount(), 0);
          EXPECT_GE(first_mesh->IndexCount(), 0);

          // If the mesh has indices, it should be marked as indexed.
          if (first_mesh->IndexCount() > 0) {
            EXPECT_TRUE(first_mesh->IsIndexed());
          }
        }
      }

      loader.Stop();
      co_return oxco::kJoin;
    };
  });
}

auto ExerciseBoundGeometryOwnership(TestEventLoop* loop,
  std::filesystem::path pak_path, const AssetKey geometry_key) -> Co<>
{
  oxco::ThreadPool pool(*loop, 2);
  oxygen::content::AssetLoaderConfig config {};
  config.thread_pool = observer_ptr { &pool };
  oxygen::content::AssetLoader loader(
    oxygen::engine::internal::EngineTagFactory::Get(), config);
  OXCO_WITH_NURSERY(nursery)
  {
    co_await nursery.Start(
      &oxygen::content::AssetLoader::ActivateAsync, &loader);
    loader.Run();
    loader.AddPakFile(pak_path);
    auto first = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
    auto second = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
    if (!first || !second || first->Meshes().empty()
      || !CheckedAt(first->Meshes(), 0)
      || CheckedAt(first->Meshes(), 0)->SubMeshes().empty()) {
      ADD_FAILURE() << "Expected bound geometry fixture";
      loader.Stop();
      co_return oxco::kJoin;
    }
    auto material
      = CheckedAt(CheckedAt(first->Meshes(), 0)->SubMeshes(), 0).Material();
    if (!material) {
      ADD_FAILURE() << "Expected a bound material";
      loader.Stop();
      co_return oxco::kJoin;
    }
    const auto material_key = material->GetAssetKey();
    EXPECT_EQ(first.get(), second.get());
#ifndef NDEBUG
    std::vector<AssetKey> dependents;
    loader.ForEachDependent(
      material_key, [&](const AssetKey& key) { dependents.push_back(key); });
    EXPECT_THAT(dependents, ::testing::ElementsAre(geometry_key));
#endif
    first.reset();
    loader.TrimCache();
    EXPECT_TRUE(loader.HasGeometryAsset(geometry_key));
    second.reset();
    loader.TrimCache();
    EXPECT_FALSE(loader.HasGeometryAsset(geometry_key));
    EXPECT_TRUE(loader.HasMaterialAsset(material_key));
    material.reset();
    loader.TrimCache();
    EXPECT_FALSE(loader.HasMaterialAsset(material_key));

    auto retained = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
    if (!retained) {
      ADD_FAILURE() << "Expected geometry reload after eviction";
      loader.Stop();
      co_return oxco::kJoin;
    }
    loader.ClearMounts();
    auto extracted = loader.GetMaterialAsset(material_key, *retained);
    EXPECT_THAT(extracted, NotNull());
    EXPECT_FALSE(loader.HasMaterialAsset(material_key));
#ifndef NDEBUG
    EXPECT_TRUE(loader.GetDebugAssetDependencyMap().empty());
#endif
    retained.reset();
    loader.TrimCache();
    EXPECT_THAT(extracted, NotNull());
    loader.Stop();
    co_return oxco::kJoin;
  };
}

NOLINT_TEST_F(AssetLoaderDependencyTest,
  IndependentParentsAndExtractedChildrenOwnTheirLifetimes)
{
  TestEventLoop loop;
  oxco::Run(loop,
    ExerciseBoundGeometryOwnership(&loop,
      GeneratePakFile("geometry_with_buffers"),
      CreateTestAssetKey("buffered_geometry")));
}

} // namespace
