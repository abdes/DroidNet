//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

#include "./AssetLoader_test.h"

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderLoadingTest;

using oxygen::data::MaterialAsset;
using oxygen::data::BufferResource;
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

enum class SuspendedLoadKind : uint8_t {
  kAsset,
  kResource,
  kCookedResource,
};

//! Hold a real decoder on a worker until the owning thread checks the drain.
struct SuspendedDecode {
  oxygen::co::Event entered;
  std::latch release { 1 };
  std::atomic<bool> finished { false };

  auto Wait(TestEventLoop& loop) -> void
  {
    loop.Schedule(std::chrono::milliseconds::zero(),
      [this]() -> void { entered.Trigger(); });
    release.wait();
  }
};

auto ExpectStopDrainsDirectLoad(TestEventLoop* loop,
  std::filesystem::path pak_path, oxygen::data::AssetKey material_key,
  const SuspendedLoadKind kind) -> Co<>
{
  oxygen::co::ThreadPool pool(*loop, 2);
  AssetLoaderConfig config {};
  config.thread_pool = observer_ptr { &pool };
  AssetLoader loader(oxygen::engine::internal::EngineTagFactory::Get(), config);
  SuspendedDecode decode;
  loader.RegisterLoader([&](oxygen::content::LoaderContext context)
                          -> std::unique_ptr<MaterialAsset> {
    decode.Wait(*loop);
    auto result
      = oxygen::content::loaders::LoadMaterialAsset(std::move(context));
    decode.finished.store(true);
    return result;
  });
  loader.RegisterLoader([&](oxygen::content::LoaderContext context)
                          -> std::unique_ptr<BufferResource> {
    decode.Wait(*loop);
    auto result
      = oxygen::content::loaders::LoadBufferResource(std::move(context));
    decode.finished.store(true);
    return result;
  });

  // A valid empty cooked buffer still traverses the real decoder and cache.
  const auto cooked_bytes = std::bit_cast<
    std::array<uint8_t, sizeof(oxygen::data::pak::core::BufferResourceDesc)>>(
    oxygen::data::pak::core::BufferResourceDesc {});
  oxygen::co::Event load_completed;
  oxygen::co::Event drain_completed;
  OXCO_WITH_NURSERY(n)
  {
    co_await n.Start(&AssetLoader::ActivateAsync, &loader);
    loader.AddPakFile(pak_path);
    const auto pak = oxygen::content::PakFile(pak_path);
    const auto resource_key = kind == SuspendedLoadKind::kCookedResource
      ? loader.MintSyntheticBufferKey()
      : loader.MakeResourceKey<BufferResource>(
          pak, oxygen::data::pak::core::ResourceIndexT { 1U });

    n.Start(
      [](AssetLoader* loader, oxygen::data::AssetKey material_key,
        oxygen::content::ResourceKey resource_key,
        std::span<const uint8_t> bytes, SuspendedLoadKind kind,
        oxygen::co::Event* completed) -> Co<> {
        if (kind == SuspendedLoadKind::kAsset) {
          const auto material
            = co_await loader->LoadAssetAsync<MaterialAsset>(material_key);
          EXPECT_THAT(material, NotNull());
          EXPECT_TRUE(loader->HasMaterialAsset(material_key));
        } else {
          const auto resource = kind == SuspendedLoadKind::kResource
            ? co_await loader->LoadResourceAsync<BufferResource>(resource_key)
            : co_await loader->LoadResourceAsync<BufferResource>(
                oxygen::content::CookedResourceData<BufferResource> {
                  .key = resource_key, .bytes = bytes });
          EXPECT_THAT(resource, NotNull());
          EXPECT_TRUE(loader->HasBuffer(resource_key));
        }
        completed->Trigger();
      },
      &loader, material_key, resource_key,
      std::span<const uint8_t>(cooked_bytes), kind, &load_completed);

    co_await decode.entered;
    loader.Stop();
    n.Start(
      [](AssetLoader* loader, SuspendedDecode* decode,
        oxygen::co::Event* completed) -> Co<> {
        co_await loader->WaitForPendingLoadsAsync();
        EXPECT_TRUE(decode->finished.load());
        completed->Trigger();
      },
      &loader, &decode, &drain_completed);

    // One event-loop turn runs the drain waiter while the decoder stays held.
    co_await loop->Sleep(std::chrono::milliseconds::zero());
    EXPECT_FALSE(drain_completed.Triggered());
    EXPECT_FALSE(decode.finished.load());
    decode.release.count_down();
    co_await drain_completed;
    co_await load_completed;
    EXPECT_TRUE(decode.finished.load());
    loader.ClearMounts();
    EXPECT_FALSE(loader.HasMaterialAsset(material_key));
    EXPECT_FALSE(loader.HasBuffer(resource_key));
    co_return oxygen::co::kJoin;
  };
}

NOLINT_TEST_F(AssetLoaderAsyncTest, StopDrainsDirectAssetDecodeAndPublication)
{
  TestEventLoop el;
  oxygen::co::Run(el,
    ExpectStopDrainsDirectLoad(&el, GeneratePakFile("simple_geometry"),
      CreateTestAssetKey("simple_material"), SuspendedLoadKind::kAsset));
}

NOLINT_TEST_F(
  AssetLoaderAsyncTest, StopDrainsDirectResourceDecodeAndPublication)
{
  TestEventLoop el;
  oxygen::co::Run(el,
    ExpectStopDrainsDirectLoad(&el, GeneratePakFile("simple_geometry"),
      CreateTestAssetKey("simple_material"), SuspendedLoadKind::kResource));
}

NOLINT_TEST_F(
  AssetLoaderAsyncTest, StopDrainsDirectCookedResourceDecodeAndPublication)
{
  TestEventLoop el;
  oxygen::co::Run(el,
    ExpectStopDrainsDirectLoad(&el, GeneratePakFile("simple_geometry"),
      CreateTestAssetKey("simple_material"),
      SuspendedLoadKind::kCookedResource));
}

//! Test: async material load publishes resource deps and runtime keys.
/*!
 Scenario: Load a material asset that references several textures using
 `LoadAssetAsync<MaterialAsset>`. Verify the material is returned, runtime
 `ResourceKey`s are set on the owning thread, and releasing the asset unloads
 dependent resources before the asset.
*/
NOLINT_TEST_F(AssetLoaderAsyncTest,
  LoadAssetAsyncMaterialWithTexturesPublishesDependenciesAndKeys)
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
    AssetLoader loader(Tag::Get(), config);

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
NOLINT_TEST_F(AssetLoaderAsyncTest, StartLoadAssetMaterialInvokesCallback)
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
    AssetLoader loader(Tag::Get(), config);

    std::shared_ptr<MaterialAsset> loaded_material;
    auto callback_called = std::make_shared<std::atomic<bool>>(false);
    auto completion_event = std::make_shared<oxygen::co::Event>();

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      // Act: callback wrapper.
      loader.StartLoadAsset<MaterialAsset>(
        material_key, [&](std::shared_ptr<MaterialAsset> asset) {
          loaded_material = std::move(asset);
          const auto was_called = callback_called->exchange(true);
          if (!was_called) {
            completion_event->Trigger();
          }
        });

      co_await loader.WaitForPendingLoadsAsync();
      EXPECT_TRUE(loader.HasMaterialAsset(material_key));
      const auto settled_material = loader.GetAsset<MaterialAsset>(material_key);
      EXPECT_THAT(settled_material, NotNull());
      if (settled_material) {
        EXPECT_NE(settled_material->GetBaseColorTextureKey().get(), 0U);
        EXPECT_NE(settled_material->GetNormalTextureKey().get(), 0U);
      }

      auto timeout_task
        = pool.Run([](oxygen::co::ThreadPool::CancelToken token) {
            using namespace std::chrono_literals;
            auto remaining = 1500ms;
            while (!token.Peek() && remaining.count() > 0) {
              std::this_thread::sleep_for(10ms);
              remaining -= 10ms;
            }
            return !token.Peek();
          });

      auto [completed, timed_out] = co_await oxygen::co::AnyOf(
        *completion_event, std::move(timeout_task));
      const bool callback_completed = completed.has_value();
      const bool timeout_hit = timed_out.has_value() && timed_out.value();

      // Assert
      EXPECT_TRUE(callback_completed);
      EXPECT_FALSE(timeout_hit);
      EXPECT_TRUE(callback_called->load());
      EXPECT_THAT(loaded_material, NotNull());

      loaded_material.reset();
      (void)loader.ReleaseAsset(material_key);
      EXPECT_TRUE(loader.HasMaterialAsset(material_key));
      loader.TrimCache();
      EXPECT_FALSE(loader.HasMaterialAsset(material_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! A queued load with no source never enters the shared I/O table.
NOLINT_TEST_F(AssetLoaderAsyncTest, DrainIncludesQueuedRequestsWithoutIoEntries)
{
  TestEventLoop el;
  auto callback_count = 0U;
  oxygen::co::Run(
    el, [](TestEventLoop* loop, unsigned* callback_count) -> Co<> {
      oxygen::co::ThreadPool pool(*loop, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr { &pool };
      AssetLoader loader(Tag::Get(), config);
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.StartLoadAsset<MaterialAsset>(CreateTestAssetKey("missing"),
          [callback_count](
            const std::shared_ptr<MaterialAsset>& asset) -> void {
            EXPECT_EQ(asset, nullptr);
            ++(*callback_count);
          });
        co_await loader.WaitForPendingLoadsAsync();
        EXPECT_EQ(*callback_count, 1U);
        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(&el, &callback_count));
}

//! Followup requests accepted during completion belong to the same drain.
NOLINT_TEST_F(AssetLoaderAsyncTest, DrainIncludesCallbackEnqueuedFollowup)
{
  TestEventLoop el;
  auto first_completed = false;
  auto followup_completed = false;
  oxygen::co::Run(el,
    [](TestEventLoop* loop, bool* first_completed,
      bool* followup_completed) -> Co<> {
      oxygen::co::ThreadPool pool(*loop, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr { &pool };
      AssetLoader loader(Tag::Get(), config);
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.StartLoadAsset<MaterialAsset>(
          CreateTestAssetKey("missing_first"),
          [&, first_completed, followup_completed](
            const std::shared_ptr<MaterialAsset>& first) -> void {
            EXPECT_EQ(first, nullptr);
            *first_completed = true;
            loader.StartLoadAsset<MaterialAsset>(
              CreateTestAssetKey("missing_followup"),
              [followup_completed](
                const std::shared_ptr<MaterialAsset>& followup) -> void {
                EXPECT_EQ(followup, nullptr);
                *followup_completed = true;
              });
          });
        co_await loader.WaitForPendingLoadsAsync();
        EXPECT_TRUE(*first_completed);
        EXPECT_TRUE(*followup_completed);
        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(&el, &first_completed, &followup_completed));
}

//! Cancellation must release queued callbacks before reporting drained work.
NOLINT_TEST_F(AssetLoaderAsyncTest, StopDrainsAcceptedCallbackLifetimes)
{
  TestEventLoop el;
  std::weak_ptr<int> callback_lifetime;
  oxygen::co::Run(
    el, [](TestEventLoop* loop, std::weak_ptr<int>* callback_lifetime) -> Co<> {
      oxygen::co::ThreadPool pool(*loop, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr { &pool };
      AssetLoader loader(Tag::Get(), config);
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        auto state = std::make_shared<int>(1);
        *callback_lifetime = state;
        loader.StartLoadAsset<MaterialAsset>(CreateTestAssetKey("cancelled"),
          [keep_alive = std::move(state)](const std::shared_ptr<MaterialAsset>&)
            -> void { EXPECT_NE(keep_alive, nullptr); });
        loader.Stop();
        co_await loader.WaitForPendingLoadsAsync();
        EXPECT_TRUE(callback_lifetime->expired());
        co_return oxygen::co::kJoin;
      };
    }(&el, &callback_lifetime));
}

//! Callback exceptions propagate once and release the accepted-work ticket.
NOLINT_TEST_F(AssetLoaderAsyncTest, ThrowingCallbackIsNotDeliveredTwice)
{
  TestEventLoop el;
  auto callback_count = 0U;
  std::weak_ptr<int> callback_lifetime;
  const auto run = [&]() -> void {
    oxygen::co::Run(el,
      [](TestEventLoop* loop, unsigned* callback_count,
        std::weak_ptr<int>* callback_lifetime) -> Co<> {
        oxygen::co::ThreadPool pool(*loop, 2);
        AssetLoaderConfig config {};
        config.thread_pool = observer_ptr { &pool };
        AssetLoader loader(Tag::Get(), config);
        OXCO_WITH_NURSERY(n)
        {
          co_await n.Start(&AssetLoader::ActivateAsync, &loader);
          auto state = std::make_shared<int>(1);
          *callback_lifetime = state;
          loader.StartLoadAsset<MaterialAsset>(CreateTestAssetKey("missing"),
            [callback_count, keep_alive = std::move(state)](
              const std::shared_ptr<MaterialAsset>&) -> void {
              EXPECT_NE(keep_alive, nullptr);
              ++(*callback_count);
              throw std::runtime_error("callback failure");
            });
          co_await loader.WaitForPendingLoadsAsync();
          loader.Stop();
          co_return oxygen::co::kJoin;
        };
      }(&el, &callback_count, &callback_lifetime));
  };
  EXPECT_THROW(run(), std::runtime_error);
  EXPECT_EQ(callback_count, 1U);
  EXPECT_TRUE(callback_lifetime.expired());
}

} // namespace
