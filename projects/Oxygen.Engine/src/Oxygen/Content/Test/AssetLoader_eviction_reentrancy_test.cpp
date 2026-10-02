//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "./AssetLoader_test.h"
#include "Fixtures/AssetLoaderSources.h"
#include "Utils/PakUtils.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::CookedResourceData;
using oxygen::content::EvictionEvent;
using oxygen::content::EvictionReason;
using oxygen::content::ResourceKey;
using oxygen::content::testing::AssetLoaderBasicTest;
using oxygen::data::BufferResource;

namespace {

auto PopulateIdleLoader(AssetLoader* loader, const ResourceKey key) -> Co<>
{
  const auto bytes = std::bit_cast<
    std::array<uint8_t, sizeof(oxygen::data::pak::core::BufferResourceDesc)>>(
    oxygen::data::pak::core::BufferResourceDesc {});
  OXCO_WITH_NURSERY(nursery)
  {
    co_await nursery.Start(&AssetLoader::ActivateAsync, loader);
    loader->Run();
    auto buffer = co_await loader->LoadResourceAsync<BufferResource>(
      CookedResourceData<BufferResource> { .key = key, .bytes = bytes });
    EXPECT_NE(buffer, nullptr);
    buffer.reset();
    co_await loader->WaitForPendingLoadsAsync();
    // Closing the parent nursery is a supported LiveObject deactivation path.
    co_return oxygen::co::kCancel;
  };
}

NOLINT_TEST_F(AssetLoaderBasicTest, EvictionCallbackCanDestroyAnIdleLoader)
{
  enum class Action : uint8_t { kTrim, kClear, kStop };
  for (const auto action : { Action::kTrim, Action::kClear, Action::kStop }) {
    TestEventLoop loop;
    oxygen::co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    auto loader = std::make_unique<AssetLoader>(Tag::Get(), config);
    const auto key = loader->MintSyntheticBufferKey();
    oxygen::co::Run(loop, PopulateIdleLoader(loader.get(), key));
    EXPECT_FALSE(loader->IsRunning());
    unsigned int calls = 0;
    auto subscription = loader->SubscribeResourceEvictions(
      BufferResource::ClassTypeId(), [&](const EvictionEvent&) -> void {
        ++calls;
        loader.reset();
      });
    switch (action) {
    case Action::kTrim:
      loader->TrimCache();
      break;
    case Action::kClear:
      loader->ClearMounts();
      break;
    case Action::kStop:
      loader->Stop();
      break;
    }
    EXPECT_FALSE(loader);
    EXPECT_EQ(calls, 1U);
  }
}

NOLINT_TEST_F(
  AssetLoaderBasicTest, ShutdownCallbackCanStopAgainAndStartAFreshEpoch)
{
  TestEventLoop loop;
  oxygen::co::ThreadPool pool(loop, 2);
  AssetLoaderConfig config {};
  config.thread_pool = observer_ptr { &pool };
  AssetLoader loader(Tag::Get(), config);
  const auto key = loader.MintSyntheticBufferKey();
  oxygen::co::Run(loop, PopulateIdleLoader(&loader, key));
  unsigned int calls = 0;
  auto subscription = loader.SubscribeResourceEvictions(
    BufferResource::ClassTypeId(), [&](const EvictionEvent&) -> void {
      ++calls;
      loader.Stop();
      loader.Run();
    });
  loader.Stop();
  EXPECT_EQ(calls, 1U);
  oxygen::co::Run(loop, PopulateIdleLoader(&loader, key));
  EXPECT_TRUE(loader.HasBuffer(key));
  loader.TrimCache();
  EXPECT_FALSE(loader.HasBuffer(key));
  EXPECT_EQ(calls, 1U);
}

auto MakeBytesFromHexdump(const std::string& hexdump, const std::size_t size,
  const uint8_t fill) -> std::vector<uint8_t>
{
  // Minimal copy of helper used by other eviction tests to build valid
  // resource payloads for buffer tests.
  auto header = oxygen::content::testing::ParseHexDumpWithOffset(hexdump);

  std::vector<uint8_t> bytes(size, fill);
  const auto copy_count = std::min(bytes.size(), header.size());
  for (std::size_t i = 0; i < copy_count; ++i) {
    bytes.at(i) = static_cast<uint8_t>(header.at(i));
  }

  return bytes;
}

auto LoadThenTrim(AssetLoader* loader, const ResourceKey key,
  std::vector<uint8_t> bytes) -> Co<>
{
  auto resource = co_await loader->LoadResourceAsync<BufferResource>(
    CookedResourceData<BufferResource> {
      .key = key,
      .bytes = bytes,
    });
  EXPECT_NE(resource, nullptr);
  resource.reset();
  loader->TrimCache();
}

// Regression test: unsubscribing from inside an eviction callback must not
// invalidate iteration or skip other subscribers in the same dispatch.
NOLINT_TEST_F(
  AssetLoaderBasicTest, ResourceEvictionCallbackSelfUnsubscribeExpectedSafe)
{

  TestEventLoop el;

  oxygen::co::Run(el, [](TestEventLoop* loop) -> Co<> {
    auto& el = *loop;
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(Tag::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      auto make_payload = [] -> std::vector<uint8_t> {
        const std::string hexdump = R"(
           0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
          16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
        )";
        constexpr std::size_t kDataOffset = 256;
        constexpr std::size_t kSizeBytes = 192;
        constexpr uint8_t kFill = 0x6A;
        return MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      };

      std::atomic<int> self_count { 0 };
      std::atomic<int> other_count { 0 };

      auto self_subscription = std::make_unique<
        oxygen::content::IAssetLoader::EvictionSubscription>();
      *self_subscription = loader.SubscribeResourceEvictions(
        BufferResource::ClassTypeId(), [&](const EvictionEvent&) -> void {
          self_count.fetch_add(1, std::memory_order_relaxed);
          self_subscription->Cancel();
        });

      auto other_subscription = loader.SubscribeResourceEvictions(
        BufferResource::ClassTypeId(), [&](const EvictionEvent&) -> void {
          other_count.fetch_add(1, std::memory_order_relaxed);
        });

      co_await LoadThenTrim(
        &loader, loader.MintSyntheticBufferKey(), make_payload());
      co_await LoadThenTrim(
        &loader, loader.MintSyntheticBufferKey(), make_payload());

      EXPECT_EQ(self_count.load(std::memory_order_relaxed), 1);
      EXPECT_EQ(other_count.load(std::memory_order_relaxed), 2);

      loader.Stop();
      (void)other_subscription;
      co_return oxygen::co::kJoin;
    };
  }(&el));
}

// Regression test: subscribing during callback must not join current dispatch,
// and should participate in subsequent evictions.
NOLINT_TEST_F(AssetLoaderBasicTest,
  ResourceEvictionCallbackSubscribeDuringDispatchExpectedNextDispatchOnly)
{

  TestEventLoop el;

  oxygen::co::Run(el, [](TestEventLoop* loop) -> Co<> {
    auto& el = *loop;
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(Tag::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      auto make_payload = [] -> std::vector<uint8_t> {
        const std::string hexdump = R"(
           0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
          16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
        )";
        constexpr std::size_t kDataOffset = 256;
        constexpr std::size_t kSizeBytes = 192;
        constexpr uint8_t kFill = 0x3C;
        return MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      };

      std::atomic<int> first_count { 0 };
      std::atomic<int> late_count { 0 };
      auto late_subscription = std::make_unique<
        oxygen::content::IAssetLoader::EvictionSubscription>();

      auto first_subscription = loader.SubscribeResourceEvictions(
        BufferResource::ClassTypeId(), [&](const EvictionEvent&) -> void {
          const auto prior
            = first_count.fetch_add(1, std::memory_order_relaxed);
          if (prior == 0) {
            *late_subscription = loader.SubscribeResourceEvictions(
              BufferResource::ClassTypeId(), [&](const EvictionEvent&) -> void {
                late_count.fetch_add(1, std::memory_order_relaxed);
              });
          }
        });

      co_await LoadThenTrim(
        &loader, loader.MintSyntheticBufferKey(), make_payload());
      co_await LoadThenTrim(
        &loader, loader.MintSyntheticBufferKey(), make_payload());

      EXPECT_EQ(first_count.load(std::memory_order_relaxed), 2);
      EXPECT_EQ(late_count.load(std::memory_order_relaxed), 1);

      loader.Stop();
      (void)first_subscription;
      co_return oxygen::co::kJoin;
    };
  }(&el));
}

auto PopulateFailedReload(AssetLoader* loader, std::filesystem::path root,
  oxygen::data::AssetKey key) -> Co<>
{
  OXCO_WITH_NURSERY(nursery)
  {
    co_await nursery.Start(&AssetLoader::ActivateAsync, loader);
    loader->Run();
    loader->AddLooseCookedRoot(root);
    auto script
      = co_await loader->LoadAssetAsync<oxygen::data::ScriptAsset>(key);
    EXPECT_NE(script, nullptr);
    loader->RegisterLoader(
      [](const oxygen::content::LoaderContext&)
        -> std::unique_ptr<oxygen::data::ScriptAsset> { return {}; });
    loader->ReloadAllScripts();
    co_await loader->WaitForPendingLoadsAsync();
    EXPECT_FALSE(loader->HasScriptAsset(key));
    co_return oxygen::co::kCancel;
  };
}

NOLINT_TEST_F(AssetLoaderBasicTest, UncachedEvictionFlushCanDestroyLoader)
{
  TestEventLoop loop;
  oxygen::co::ThreadPool pool(loop, 2);
  AssetLoaderConfig config {};
  config.thread_pool = observer_ptr { &pool };
  auto loader = std::make_unique<AssetLoader>(Tag::Get(), config);
  const auto key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/Logic.oscript");
  const auto root = temp_dir_ / "source";
  oxygen::content::testing::WriteScriptSource(root, key);
  oxygen::co::Run(loop, PopulateFailedReload(loader.get(), root, key));
  unsigned int calls = 0;
  auto subscription = loader->SubscribeResourceEvictions(
    oxygen::data::ScriptResource::ClassTypeId(),
    [&](const EvictionEvent&) -> void {
      ++calls;
      loader.reset();
    });
  loader->TrimCache();
  EXPECT_FALSE(loader);
  EXPECT_EQ(calls, 1U);
}

} // namespace
