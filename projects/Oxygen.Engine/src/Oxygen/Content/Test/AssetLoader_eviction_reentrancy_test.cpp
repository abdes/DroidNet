//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

#include "./AssetLoader_test.h"
#include "Utils/PakUtils.h"

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::CookedResourceData;
using oxygen::content::EvictionEvent;
using oxygen::content::EvictionReason;
using oxygen::content::ResourceKey;
using oxygen::content::testing::AssetLoaderLoadingTest;
using oxygen::data::BufferResource;

namespace {

auto MakeBytesFromHexdump(const std::string& hexdump, const std::size_t size,
  const uint8_t fill) -> std::vector<uint8_t>
{
  // Minimal copy of helper used by other eviction tests to build valid
  // resource payloads for buffer tests.
  auto header = oxygen::content::testing::ParseHexDumpWithOffset(hexdump);

  std::vector<uint8_t> bytes(size, fill);
  const auto copy_count = std::min(bytes.size(), header.size());
  for (std::size_t i = 0; i < copy_count; ++i) {
    bytes[i] = static_cast<uint8_t>(header[i]);
  }

  return bytes;
}

// Regression test: subscriber that calls back into the loader during eviction
// must not cause a re-entrant/looping eviction notification. Handler should
// be invoked exactly once.
NOLINT_TEST_F(AssetLoaderLoadingTest, ResourceEvictionReentrantHandler)
{
  using namespace std::chrono_literals;

  TestEventLoop el;

  (oxygen::co::Run)(el, [&]() -> Co<> {
    AssetLoaderConfig config {};

    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      const auto key = loader.MintSyntheticBufferKey();

      // Build a valid buffer cooked payload similar to other tests.
      const std::string hexdump = R"(
         0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
        16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
      )";
      constexpr std::size_t kDataOffset = 256;
      constexpr std::size_t kSizeBytes = 192;
      constexpr uint8_t kFill = 0xAB;

      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      // Subscribe and request a re-entrant release from the event loop.
      // Direct re-entry from the handler would recurse into AnyCache while it
      // holds its internal lock; deferring preserves re-entry intent without
      // deadlocking on the same mutex.
      std::atomic<int> call_count { 0 };
      std::atomic<int> nested_release_calls { 0 };
      auto subscription
        = loader.SubscribeResourceEvictions(BufferResource::ClassTypeId(),
          [&](const EvictionEvent& /*ev*/) -> void {
            call_count.fetch_add(1, std::memory_order_relaxed);
            el.Schedule(0ms, [&loader, key, &nested_release_calls] {
              (void)loader.ReleaseResource(key);
              nested_release_calls.fetch_add(1, std::memory_order_relaxed);
            });
          });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key, .bytes = span });
      EXPECT_NE(resource, nullptr);

      // Drop local ref and release
      resource.reset();
      (void)loader.ReleaseResource(key);
      loader.TrimCache();
      co_await el.Sleep(0ms);

      // Handler must have been called once.
      EXPECT_EQ(call_count.load(std::memory_order_relaxed), 1);
      EXPECT_EQ(nested_release_calls.load(std::memory_order_relaxed), 1);

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

// Regression test: unsubscribing from inside an eviction callback must not
// invalidate iteration or skip other subscribers in the same dispatch.
NOLINT_TEST_F(
  AssetLoaderLoadingTest, ResourceEvictionCallbackSelfUnsubscribeExpectedSafe)
{
  using namespace std::chrono_literals;

  TestEventLoop el;

  (oxygen::co::Run)(el, [&]() -> Co<> {
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(Tag::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      auto make_payload = [] {
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
        BufferResource::ClassTypeId(), [&](const EvictionEvent&) {
          self_count.fetch_add(1, std::memory_order_relaxed);
          self_subscription->Cancel();
        });

      auto other_subscription = loader.SubscribeResourceEvictions(
        BufferResource::ClassTypeId(), [&](const EvictionEvent&) {
          other_count.fetch_add(1, std::memory_order_relaxed);
        });

      const auto evict_once = [&](const ResourceKey key) -> Co<> {
        auto bytes = make_payload();
        std::span<const uint8_t> span(bytes.data(), bytes.size());
        auto resource = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key,
            .bytes = span,
          });
        EXPECT_NE(resource, nullptr);
        resource.reset();
        (void)loader.ReleaseResource(key);
        loader.TrimCache();
        co_return;
      };

      co_await evict_once(loader.MintSyntheticBufferKey());
      co_await evict_once(loader.MintSyntheticBufferKey());

      EXPECT_EQ(self_count.load(std::memory_order_relaxed), 1);
      EXPECT_EQ(other_count.load(std::memory_order_relaxed), 2);

      loader.Stop();
      (void)other_subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

// Regression test: subscribing during callback must not join current dispatch,
// and should participate in subsequent evictions.
NOLINT_TEST_F(AssetLoaderLoadingTest,
  ResourceEvictionCallbackSubscribeDuringDispatchExpectedNextDispatchOnly)
{
  using namespace std::chrono_literals;

  TestEventLoop el;

  (oxygen::co::Run)(el, [&]() -> Co<> {
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(Tag::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      auto make_payload = [] {
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
        BufferResource::ClassTypeId(), [&](const EvictionEvent&) {
          const auto prior
            = first_count.fetch_add(1, std::memory_order_relaxed);
          if (prior == 0) {
            *late_subscription = loader.SubscribeResourceEvictions(
              BufferResource::ClassTypeId(), [&](const EvictionEvent&) {
                late_count.fetch_add(1, std::memory_order_relaxed);
              });
          }
        });

      const auto evict_once = [&](const ResourceKey key) -> Co<> {
        auto bytes = make_payload();
        std::span<const uint8_t> span(bytes.data(), bytes.size());
        auto resource = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key,
            .bytes = span,
          });
        EXPECT_NE(resource, nullptr);
        resource.reset();
        (void)loader.ReleaseResource(key);
        loader.TrimCache();
        co_return;
      };

      co_await evict_once(loader.MintSyntheticBufferKey());
      co_await evict_once(loader.MintSyntheticBufferKey());

      EXPECT_EQ(first_count.load(std::memory_order_relaxed), 2);
      EXPECT_EQ(late_count.load(std::memory_order_relaxed), 1);

      loader.Stop();
      (void)first_subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace
