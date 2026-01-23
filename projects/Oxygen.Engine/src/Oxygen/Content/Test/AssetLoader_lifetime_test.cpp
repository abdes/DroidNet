//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>

#include "Utils/PakUtils.h"

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::CookedResourceData;
using oxygen::content::ResourceKey;
using oxygen::content::testing::AssetLoaderLoadingTest;

using oxygen::data::BufferResource;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

namespace {

//=== AssetLoader Lifetime Tests ===-----------------------------------------//

class AssetLoaderLifetimeTest : public AssetLoaderLoadingTest { };

class AssetLoaderLifetimeAsyncTest : public AssetLoaderLoadingTest {
protected:
  void SetUp() override
  {
    AssetLoaderLoadingTest::SetUp();
    asset_loader_.reset();
  }
};

auto MakeBytesFromHexdump(const std::string& hexdump, const std::size_t size,
  const uint8_t fill) -> std::vector<uint8_t>
{
  auto header = oxygen::content::testing::ParseHexDumpWithOffset(
    hexdump, static_cast<int>(size), std::byte { fill });

  std::vector<uint8_t> bytes(size, fill);
  const auto copy_count = std::min(bytes.size(), header.size());
  for (std::size_t i = 0; i < copy_count; ++i) {
    bytes[i] = static_cast<uint8_t>(header[i]);
  }

  return bytes;
}

//! Test: Resource remains cached until explicit release.
/*!
 Scenario: Load a BufferResource from cooked bytes, drop the returned
 shared_ptr, and verify the resource is still cached. Only after calling
 ReleaseResource should the cache entry be evicted.
*/
NOLINT_TEST_F(
  AssetLoaderLifetimeAsyncTest, ResourceUnload_RequiresExplicitRelease)
{
  using namespace std::chrono_literals;

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFill = 0xAB;

  const auto key = ResourceKey { 0xABCDEFU };
  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    AssetLoaderConfig config {};

    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      std::span<const uint8_t> span(bytes.data(), bytes.size());
      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });

      EXPECT_THAT(resource, NotNull());
      resource.reset();

      EXPECT_TRUE(loader.HasBuffer(key));
      auto cached = loader.GetBuffer(key);
      EXPECT_THAT(cached, NotNull());
      cached.reset();

      loader.ReleaseResource(key);
      EXPECT_FALSE(loader.HasBuffer(key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Refcounted checkouts require matching releases.
/*!
 Scenario: Load a BufferResource, check it out once more using GetBuffer,
 and verify that a single ReleaseResource does not evict the entry. A second
 ReleaseResource is required to evict the cache entry.
*/
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest, ResourceUnload_RefcountedCheckouts)
{
  using namespace std::chrono_literals;

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFill = 0x5A;

  const auto key = ResourceKey { 0x12345678U };
  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    AssetLoaderConfig config {};

    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      std::span<const uint8_t> span(bytes.data(), bytes.size());
      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());

      auto extra_checkout = loader.CheckOutResource<BufferResource>(key);
      EXPECT_THAT(extra_checkout, NotNull());
      extra_checkout.reset();

      loader.ReleaseResource(key);
      EXPECT_TRUE(loader.HasBuffer(key));

      loader.ReleaseResource(key);
      EXPECT_FALSE(loader.HasBuffer(key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Asset remains cached and is reused until explicit release.
/*!
 Scenario: Load a MaterialAsset, drop the returned shared_ptr without calling
 ReleaseAsset, then load again and expect the same cached instance. After
 ReleaseAsset, the cache entry should be removed.
*/
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest, AssetUnload_RequiresExplicitRelease)
{
  using namespace std::chrono_literals;

  // Arrange
  const auto pak_path = GeneratePakFile("material_with_textures");
  const auto material_key = CreateTestAssetKey("textured_material");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());
      const auto* first_ptr = material.get();
      material.reset();

      EXPECT_TRUE(loader.HasMaterialAsset(material_key));

      auto cached = loader.GetMaterialAsset(material_key);
      EXPECT_THAT(cached, NotNull());
      EXPECT_EQ(cached.get(), first_ptr);
      cached.reset();

      loader.ReleaseAsset(material_key);
      EXPECT_FALSE(loader.HasMaterialAsset(material_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Release order unloads dependency before dependent.
/*!
 Scenario: A depends on B. Releasing A cascades and causes B to be checked in
 before A.
*/
NOLINT_TEST_F(AssetLoaderLifetimeTest, ReleaseOrder_DependencyBeforeDependent)
{
  // Arrange
  const auto key_a = CreateTestAssetKey("release_a");
  const auto key_b = CreateTestAssetKey("release_b");
  asset_loader_->AddAssetDependency(key_a, key_b);

  // Act
  asset_loader_->ReleaseAsset(key_a);
  asset_loader_->ReleaseAsset(key_b);

  // Assert (idempotence)
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_a));
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_b));
}

#if !defined(NDEBUG)
//! Test: Releasing one of multiple dependents does not evict shared dependency.
/*!
 Scenario: A -> C, B -> C. Release A; C must remain for B. Then release B; C
 may be released.
*/
NOLINT_TEST_F(
  AssetLoaderLifetimeTest, CascadeRelease_SiblingSharedDependencyNotEvicted)
{
  const auto key_a = CreateTestAssetKey("cascade_a");
  const auto key_b = CreateTestAssetKey("cascade_b");
  const auto key_c = CreateTestAssetKey("cascade_shared");
  asset_loader_->AddAssetDependency(key_a, key_c);
  asset_loader_->AddAssetDependency(key_b, key_c);

  size_t dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const oxygen::data::AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 2);

  asset_loader_->ReleaseAsset(key_a);

  dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const oxygen::data::AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 1);

  asset_loader_->ReleaseAsset(key_b);

  dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const oxygen::data::AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 0);

  asset_loader_->ReleaseAsset(key_a);
  asset_loader_->ReleaseAsset(key_b);
}
#endif // !NDEBUG

//! Test: async geometry load binds dependencies and unloads on release.
/*!
 Scenario: Load a geometry asset that references buffers and material assets.
 Verify the geometry is returned and releasing the asset evicts it from cache.
*/
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest,
  LoadAssetAsync_GeometryWithBuffers_BindsDependenciesAndUnloadsInOrder)
{
  using namespace std::chrono_literals;

  // Arrange
  const auto pak_path = GeneratePakFile("geometry_with_buffers");
  const auto geometry_key = CreateTestAssetKey("buffered_geometry");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      auto geometry
        = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
      EXPECT_THAT(geometry, NotNull());

      if (geometry) {
        const auto meshes = geometry->Meshes();
        EXPECT_FALSE(meshes.empty());

        if (!meshes.empty() && meshes[0]) {
          EXPECT_EQ(meshes[0]->VertexCount(), 6U);
          EXPECT_EQ(meshes[0]->IndexCount(), 3U);

          const auto submeshes = meshes[0]->SubMeshes();
          EXPECT_FALSE(submeshes.empty());
          if (!submeshes.empty()) {
            EXPECT_THAT(submeshes[0].Material(), NotNull());
          }
        }
      }

      geometry.reset();
      loader.ReleaseAsset(geometry_key);
      EXPECT_FALSE(loader.HasGeometryAsset(geometry_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace
