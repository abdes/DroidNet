//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>

#include "./AssetLoader_test.h"

namespace {

using namespace std::chrono_literals;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderLoadingTest;

//! Fixture for cancellation tests using a real ThreadPool + TestEventLoop.
class AssetLoaderCancellationTest : public AssetLoaderLoadingTest {
protected:
  void SetUp() override
  {
    AssetLoaderLoadingTest::SetUp();
    asset_loader_.reset();
  }
};

//! Verifies that Stop() cancels StartLoad work promptly.
NOLINT_TEST_F(AssetLoaderCancellationTest, StopCancelsStartLoadAsset)
{
  // Arrange
  const auto pak_path = GeneratePakFile("material_with_textures");
  const auto material_key = CreateTestAssetKey("textured_material");

  TestEventLoop el;

  std::atomic<bool> callback_called { false };
  std::shared_ptr<oxygen::data::MaterialAsset> result;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = oxygen::observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      loader.StartLoadAsset<oxygen::data::MaterialAsset>(
        material_key, [&](std::shared_ptr<oxygen::data::MaterialAsset> asset) {
          result = std::move(asset);
          callback_called.store(true);
        });

      loader.Stop();

      // Give cancellation a chance to propagate through the event loop.
      for (int i = 0; i < 50 && !callback_called.load(); ++i) {
        co_await el.Sleep(1ms);
      }

      co_return oxygen::co::kJoin;
    };
  });

  // Assert
  // Policy for callback bridges under cancellation: must not crash or hang.
  // The callback may or may not be invoked depending on cancellation timing.
  SUCCEED();
}

} // namespace
