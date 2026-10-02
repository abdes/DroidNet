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
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "./AssetLoader_test.h"
#include "Fixtures/AssetLoaderSources.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderBasicTest;

using oxygen::data::BufferResource;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

namespace {

//! Fixture for async AssetLoader tests using a real ThreadPool + TestEventLoop.
class AssetLoaderAsyncTest : public AssetLoaderBasicTest {
protected:
  void SetUp() override
  {
    AssetLoaderBasicTest::SetUp();

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

enum class CallbackCompletion : uint8_t { kPublish, kCancel };

//! Hold a real decoder on a worker until the owning thread checks the drain.
struct SuspendedDecode {
  oxygen::co::Event entered;
  std::latch release { 1 };
  std::atomic<bool> finished { false };

  auto Wait(TestEventLoop& loop) -> void
  {
    loop.Schedule(
      std::chrono::milliseconds::zero(), [this] -> void { entered.Trigger(); });
    release.wait();
  }
};

auto CheckCallbackCompletion(TestEventLoop* loop, std::filesystem::path root,
  oxygen::data::AssetKey key, CallbackCompletion completion) -> Co<>
{
  oxygen::co::ThreadPool pool(*loop, 2);
  AssetLoaderConfig config {};
  config.thread_pool = observer_ptr { &pool };
  AssetLoader loader(oxygen::engine::internal::EngineTagFactory::Get(), config);
  SuspendedDecode decode;
  loader.RegisterLoader([&](oxygen::content::LoaderContext context)
                          -> std::unique_ptr<MaterialAsset> {
    decode.Wait(*loop);
    auto asset
      = oxygen::content::loaders::LoadMaterialAsset(std::move(context));
    decode.finished.store(true);
    return asset;
  });
  unsigned int calls = 0U;
  unsigned int followups = 0U;
  std::shared_ptr<MaterialAsset> published;
  std::weak_ptr<int> lifetime;
  const auto owner_thread = std::this_thread::get_id();
  OXCO_WITH_NURSERY(nursery)
  {
    co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
    loader.Run();
    loader.AddLooseCookedRoot(root);
    auto captured = std::make_shared<int>(1);
    lifetime = captured;
    loader.StartLoadAsset<MaterialAsset>(key,
      [&, keep_alive = std::move(captured)](
        std::shared_ptr<MaterialAsset> asset) -> void {
        EXPECT_EQ(std::this_thread::get_id(), owner_thread);
        EXPECT_NE(keep_alive, nullptr);
        ++calls;
        published = std::move(asset);
        if (completion == CallbackCompletion::kCancel) {
          EXPECT_EQ(published, nullptr);
          return;
        }
        loader.StartLoadAsset<MaterialAsset>(
          key, [&](const std::shared_ptr<MaterialAsset>& followup) -> void {
            ++followups;
            EXPECT_EQ(followup, published);
          });
      });
    co_await decode.entered;
    EXPECT_EQ(calls, 0U);
    EXPECT_FALSE(loader.HasMaterialAsset(key));
    EXPECT_FALSE(lifetime.expired());
    if (completion == CallbackCompletion::kCancel) {
      loader.Stop();
    }
    decode.release.count_down();
    co_await loader.WaitForPendingLoadsAsync();
    EXPECT_TRUE(decode.finished.load());
    EXPECT_TRUE(lifetime.expired());
    if (completion == CallbackCompletion::kPublish) {
      EXPECT_EQ(calls, 1U);
      EXPECT_EQ(followups, 1U);
      EXPECT_NE(published, nullptr);
      EXPECT_EQ(loader.GetMaterialAsset(key), published);
      loader.Stop();
    } else {
      EXPECT_EQ(calls, 1U);
      EXPECT_EQ(followups, 0U);
      EXPECT_EQ(published, nullptr);
      EXPECT_FALSE(loader.HasMaterialAsset(key));
    }
    co_return oxygen::co::kJoin;
  };
}

NOLINT_TEST_F(
  AssetLoaderAsyncTest, CallbackPublishesOnceAndDrainIncludesFollowup)
{
  const auto root = temp_dir_ / "source";
  const auto key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/Surface.omat");
  oxygen::content::testing::WriteMaterialSource(
    root, key, { 1.0F, 0.0F, 0.0F, 1.0F });
  TestEventLoop loop;
  oxygen::co::Run(loop,
    CheckCallbackCompletion(&loop, root, key, CallbackCompletion::kPublish));
}

NOLINT_TEST_F(
  AssetLoaderAsyncTest, StopDuringDecodeCompletesWithNullAndReleasesCapture)
{
  const auto root = temp_dir_ / "source";
  const auto key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/Surface.omat");
  oxygen::content::testing::WriteMaterialSource(
    root, key, { 1.0F, 0.0F, 0.0F, 1.0F });
  TestEventLoop loop;
  oxygen::co::Run(loop,
    CheckCallbackCompletion(&loop, root, key, CallbackCompletion::kCancel));
}

auto ExpectStopDrainsDirectLoad(TestEventLoop* loop, std::filesystem::path root,
  oxygen::data::AssetKey material_key, const SuspendedLoadKind kind) -> Co<>
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

  loader.RegisterLoader([&](oxygen::content::LoaderContext context)
                          -> std::unique_ptr<TextureResource> {
    decode.Wait(*loop);
    auto result
      = oxygen::content::loaders::LoadTextureResource(std::move(context));
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
    loader.AddLooseCookedRoot(root);
    const auto sources = loader.EnumerateMountedSources();
    if (sources.size() != 1U) {
      ADD_FAILURE() << "Expected one mounted source";
      loader.Stop();
      co_return oxygen::co::kJoin;
    }
    const auto texture_key = loader.MakeTextureResourceKey(
      sources.front().source_key, oxygen::ResourceIndexT { 1U });
    if (!texture_key) {
      ADD_FAILURE() << "Expected the authored texture binding";
      loader.Stop();
      co_return oxygen::co::kJoin;
    }
    const auto resource_key = kind == SuspendedLoadKind::kResource
      ? *texture_key
      : loader.MintSyntheticBufferKey();

    n.Start(
      [](AssetLoader* loader, oxygen::data::AssetKey material_key,
        oxygen::content::ResourceKey resource_key,
        std::span<const uint8_t> bytes, SuspendedLoadKind kind,
        oxygen::co::Event* completed) -> Co<> {
        bool cancelled = false;
        try {
          if (kind == SuspendedLoadKind::kAsset) {
            static_cast<void>(
              co_await loader->LoadAssetAsync<MaterialAsset>(material_key));
          } else if (kind == SuspendedLoadKind::kResource) {
            static_cast<void>(
              co_await loader->LoadResourceAsync<TextureResource>(
                resource_key));
          } else {
            static_cast<void>(
              co_await loader->LoadResourceAsync<BufferResource>(
                oxygen::content::CookedResourceData<BufferResource> {
                  .key = resource_key,
                  .bytes = bytes,
                }));
          }
        } catch (const oxygen::content::OperationCancelledException&) {
          cancelled = true;
        }
        EXPECT_TRUE(cancelled);
        EXPECT_FALSE(loader->HasMaterialAsset(material_key));
        EXPECT_FALSE(loader->HasBuffer(resource_key));
        EXPECT_FALSE(loader->HasTexture(resource_key));
        completed->Trigger();
      },
      &loader, material_key, resource_key,
      std::span<const uint8_t>(cooked_bytes), kind, &load_completed);

    co_await decode.entered;
    loader.Stop();
    loader.Run();
    n.Start(
      [](AssetLoader* loader, SuspendedDecode* decode,
        oxygen::co::Event* completed) -> Co<> {
        co_await loader->WaitForPendingLoadsAsync();
        EXPECT_TRUE(decode->finished.load());
        completed->Trigger();
      },
      &loader, &decode, &drain_completed);

    // One event-loop turn runs the drain waiter while the decoder stays held.
    co_await oxygen::co::kYield;
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
  const auto root = temp_dir_ / "source";
  const auto key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/Surface.omat");
  oxygen::content::testing::WriteTexturedMaterialSource(root, key);
  TestEventLoop el;
  oxygen::co::Run(
    el, ExpectStopDrainsDirectLoad(&el, root, key, SuspendedLoadKind::kAsset));
}

NOLINT_TEST_F(
  AssetLoaderAsyncTest, StopDrainsDirectResourceDecodeAndPublication)
{
  const auto root = temp_dir_ / "source";
  const auto key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/Surface.omat");
  oxygen::content::testing::WriteTexturedMaterialSource(root, key);
  TestEventLoop el;
  oxygen::co::Run(el,
    ExpectStopDrainsDirectLoad(&el, root, key, SuspendedLoadKind::kResource));
}

NOLINT_TEST_F(
  AssetLoaderAsyncTest, StopDrainsDirectCookedResourceDecodeAndPublication)
{
  const auto root = temp_dir_ / "source";
  const auto key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/Surface.omat");
  oxygen::content::testing::WriteTexturedMaterialSource(root, key);
  TestEventLoop el;
  oxygen::co::Run(el,
    ExpectStopDrainsDirectLoad(
      &el, root, key, SuspendedLoadKind::kCookedResource));
}

//! Missing-source callback exceptions propagate once and release captures.
NOLINT_TEST_F(
  AssetLoaderAsyncTest, MissingSourceCallbackExceptionPropagatesOnce)
{
  TestEventLoop el;
  auto callback_count = 0U;
  std::weak_ptr<int> callback_lifetime;
  const auto run = [&] -> void {
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
          loader.StartLoadAsset<MaterialAsset>(
            oxygen::data::AssetKey::FromVirtualPath("/Test/missing.asset"),
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
