//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./AssetLoader_test.h"

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>

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
NOLINT_TEST_F(AssetLoaderLoadingTest, ResourceEviction_ReentrantHandler)
{
  using namespace std::chrono_literals;

  TestEventLoop el;

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

      // Subscribe and in the handler schedule ReleaseResource(key) on the
      // TestEventLoop so the callback executes on the loader's owning thread.
      std::atomic<int> call_count { 0 };
      auto subscription = loader.SubscribeResourceEvictions(
        BufferResource::ClassTypeId(), [&](const EvictionEvent& /*ev*/) {
          call_count.fetch_add(1, std::memory_order_relaxed);
          el.Schedule(milliseconds { 0 },
            [&loader, key] { (void)loader.ReleaseResource(key); });
        });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key, .bytes = span });
      EXPECT_NE(resource, nullptr);

      // Drop local ref and release
      resource.reset();
      loader.ReleaseResource(key);

      // Allow scheduled event loop callbacks to run and perform the nested
      // ReleaseResource scheduled by the handler. Use a short sleep to yield
      // control back to the TestEventLoop.
      co_await el.Sleep(milliseconds { 0 });

      // Handler must have been called once.
      EXPECT_EQ(call_count.load(std::memory_order_relaxed), 1);

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace
