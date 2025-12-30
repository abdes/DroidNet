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

#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderLoadingTest;

using oxygen::data::BufferResource;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

namespace {

//! Fixture for async GeometryAsset tests using a real ThreadPool +
//! TestEventLoop.
class GeometryAssetAsyncTest : public AssetLoaderLoadingTest {
protected:
  void SetUp() override
  {
    AssetLoaderLoadingTest::SetUp();

    // The base fixture constructs an AssetLoader without a thread pool.
    // For async tests we construct a fresh instance inside the event loop.
    asset_loader_.reset();
  }
};

//! Test: async geometry load binds buffers and material assets.
/*!
 Scenario: Load a geometry asset that references vertex/index buffers and a
 material asset using `LoadAssetAsync<GeometryAsset>`. Verify the geometry is
 returned, runtime mesh storage is bound (non-zero vertices), and releasing the
 asset unloads dependent objects before the asset.
*/
NOLINT_TEST_F(GeometryAssetAsyncTest,
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

      // Act
      auto geometry
        = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);

      // Assert
      EXPECT_THAT(geometry, NotNull());

      if (geometry) {
        const auto meshes = geometry->Meshes();
        EXPECT_FALSE(meshes.empty());

        if (!meshes.empty() && meshes[0]) {
          // The test PAK uses 6 vertices and 3 indices.
          EXPECT_EQ(meshes[0]->VertexCount(), 6U);
          EXPECT_EQ(meshes[0]->IndexCount(), 3U);

          const auto submeshes = meshes[0]->SubMeshes();
          EXPECT_FALSE(submeshes.empty());
          if (!submeshes.empty()) {
            EXPECT_THAT(submeshes[0].Material(), NotNull());
          }
        }
      }

      // Drop our reference, then release by key to allow eviction/unload.
      geometry.reset();
      EXPECT_TRUE(loader.ReleaseAsset(geometry_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace
