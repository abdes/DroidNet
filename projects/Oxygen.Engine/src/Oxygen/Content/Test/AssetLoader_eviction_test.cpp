//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "./AssetLoader_test.h"
#include "Utils/PakUtils.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
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
using oxygen::content::CookedResourceData;
using oxygen::content::EvictionEvent;
using oxygen::content::EvictionReason;
using oxygen::content::ResourceKey;
using oxygen::content::testing::AssetLoaderBasicTest;

using oxygen::data::BufferResource;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

namespace {

auto MakeBytesFromHexdump(const std::string& hexdump, const std::size_t size,
  const uint8_t fill) -> std::vector<uint8_t>
{
  auto header = oxygen::content::testing::ParseHexDumpWithOffset(hexdump);

  std::vector<uint8_t> bytes(size, fill);
  const auto copy_count = std::min(bytes.size(), header.size());
  for (std::size_t i = 0; i < copy_count; ++i) {
    bytes.at(i) = static_cast<uint8_t>(header.at(i));
  }

  return bytes;
}

//! Fixture for eviction notification tests.
class AssetLoaderEvictionAsyncTest : public AssetLoaderBasicTest {
protected:
  void SetUp() override
  {
    AssetLoaderBasicTest::SetUp();
    asset_loader_.reset();
  }
};

//! Regression: repeated TrimCache passes must remain stable after evictions.
/*!
 Scenario: repeatedly load/release a textured material and force TrimCache.
 This stresses resource-map traversal while eviction callbacks mutate mappings.
 The test verifies each cycle evicts exactly the expected texture set and that
 additional no-op trims do not emit extra events.
*/

//! Regression: refresh must not leave stale uncached resource mappings.
/*!
 Scenario: Load a textured material, then refresh the same mounted PAK path.
 Refresh performs a destructive cache clear and eviction flush. Repeated
 TrimCache calls after each refresh must not emit duplicate texture evictions.
*/

NOLINT_TEST_F(
  AssetLoaderEvictionAsyncTest, ResourceEvictionNotifiesSubscriberOnRelease)
{
  using namespace std::chrono_literals;

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  static constexpr uint8_t kFill = 0xAB;

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [](std::string hexdump, TestEventLoop* loop) -> Co<> {
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

      const auto key = loader.MintSyntheticBufferKey();
      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(BufferResource::ClassTypeId(),
          [&](const EvictionEvent& event) -> void { events.push_back(event); });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      loader.TrimCache();

      EXPECT_EQ(events.size(), 1U);
      if (!events.empty()) {
        EXPECT_EQ(events.front().key, key);
        EXPECT_EQ(events.front().type_id, BufferResource::ClassTypeId());
        EXPECT_EQ(events.front().reason, EvictionReason::kTrim);
      }

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  }(hexdump, &el));
}

NOLINT_TEST_F(AssetLoaderEvictionAsyncTest, ResourceEvictionFiltersByType)
{
  using namespace std::chrono_literals;

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  static constexpr uint8_t kFill = 0x5A;

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [](std::string hexdump, TestEventLoop* loop) -> Co<> {
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

      const auto key = loader.MintSyntheticBufferKey();
      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(TextureResource::ClassTypeId(),
          [&](const EvictionEvent& event) -> void { events.push_back(event); });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      EXPECT_TRUE(events.empty());

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  }(hexdump, &el));
}

NOLINT_TEST_F(AssetLoaderEvictionAsyncTest, ResourceEvictionClearMounts)
{
  using namespace std::chrono_literals;

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  static constexpr uint8_t kFill = 0x11;

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [](std::string hexdump, TestEventLoop* loop) -> Co<> {
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

      const auto key = loader.MintSyntheticBufferKey();
      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(BufferResource::ClassTypeId(),
          [&](const EvictionEvent& event) -> void { events.push_back(event); });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      loader.ClearMounts();

      EXPECT_EQ(events.size(), 1U);
      if (!events.empty()) {
        EXPECT_EQ(events.front().reason, EvictionReason::kClear);
      }

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  }(hexdump, &el));
}

NOLINT_TEST_F(AssetLoaderEvictionAsyncTest, ResourceEvictionStop)
{
  using namespace std::chrono_literals;

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  static constexpr uint8_t kFill = 0x22;

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [](std::string hexdump, TestEventLoop* loop) -> Co<> {
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

      const auto key = loader.MintSyntheticBufferKey();
      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(BufferResource::ClassTypeId(),
          [&](const EvictionEvent& event) -> void { events.push_back(event); });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      loader.Stop();

      EXPECT_EQ(events.size(), 1U);
      if (!events.empty()) {
        EXPECT_EQ(events.front().reason, EvictionReason::kShutdown);
      }

      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  }(hexdump, &el));
}

} // namespace
