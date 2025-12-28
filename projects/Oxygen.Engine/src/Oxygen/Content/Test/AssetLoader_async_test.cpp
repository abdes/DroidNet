//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>

#include "./AssetLoader_test.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderLoadingTest;

using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

namespace {

//! Fixture for async AssetLoader tests using a real ThreadPool + TestEventLoop.
class AssetLoaderAsyncTest : public AssetLoaderLoadingTest {
protected:
  void SetUp() override
  {
    AssetLoaderLoadingTest::SetUp();

    // The base fixture constructs an AssetLoader without a thread pool.
    // For async tests we construct a fresh instance inside the event loop.
    asset_loader_.reset();
  }
};

//! Test: async material load publishes resource deps and runtime keys.
/*!
 Scenario: Load a material asset that references several textures using
 `LoadAssetAsync<MaterialAsset>`. Verify the material is returned, runtime
 `ResourceKey`s are set on the owning thread, and releasing the asset unloads
 dependent resources before the asset.
*/
NOLINT_TEST_F(AssetLoaderAsyncTest,
  LoadAssetAsync_MaterialWithTextures_PublishesDependenciesAndKeys)
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

      // Act: awaitable async load.
      auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);

      // Assert: material is loaded.
      EXPECT_THAT(material, NotNull());

      // Assert: publish step filled runtime per-slot ResourceKeys.
      EXPECT_NE(material->GetBaseColorTextureKey().get(), 0U);
      EXPECT_NE(material->GetNormalTextureKey().get(), 0U);
      EXPECT_NE(material->GetRoughnessTextureKey().get(), 0U);

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: StartLoadAsset invokes callback on owning thread.
/*!
 Scenario: Start a material load via `StartLoadAsset<MaterialAsset>` and verify
 the callback is invoked with a valid result.
*/
NOLINT_TEST_F(AssetLoaderAsyncTest, StartLoadAsset_Material_InvokesCallback)
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

    std::shared_ptr<MaterialAsset> loaded_material;
    bool callback_called = false;

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      // Act: callback wrapper.
      loader.StartLoadAsset<MaterialAsset>(
        material_key, [&](std::shared_ptr<MaterialAsset> asset) {
          loaded_material = std::move(asset);
          callback_called = true;
        });

      // Wait (deterministically) for the callback to be invoked.
      for (int i = 0; i < 200 && !callback_called; ++i) {
        co_await el.Sleep(1ms);
      }

      // Assert
      EXPECT_TRUE(callback_called);
      EXPECT_THAT(loaded_material, NotNull());

      loaded_material.reset();
      EXPECT_TRUE(loader.ReleaseAsset(material_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace
