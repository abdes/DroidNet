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
#include <unordered_set>
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
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
using oxygen::content::EvictionEvent;
using oxygen::content::EvictionReason;
using oxygen::content::ResourceKey;
using oxygen::content::testing::AssetLoaderLoadingTest;

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
    bytes[i] = static_cast<uint8_t>(header[i]);
  }

  return bytes;
}

//! Fixture for eviction notification tests.
class AssetLoaderEvictionAsyncTest : public AssetLoaderLoadingTest {
protected:
  void SetUp() override
  {
    AssetLoaderLoadingTest::SetUp();
    asset_loader_.reset();
  }
};

//! Test: Buffer eviction notifies subscribers on release.
/*!
 Scenario: Load a buffer resource from cooked bytes, drop the returned pointer,
 and release the resource. Expect a single eviction event with refcount reason.
*/
NOLINT_TEST_F(
  AssetLoaderEvictionAsyncTest, ResourceEviction_NotifiesSubscriberOnRelease)
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

      const auto key = loader.MintSyntheticBufferKey();
      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(BufferResource::ClassTypeId(),
          [&](const EvictionEvent& event) { events.push_back(event); });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      loader.ReleaseResource(key);

      EXPECT_EQ(events.size(), 1u);
      if (!events.empty()) {
        EXPECT_EQ(events.front().key, key);
        EXPECT_EQ(events.front().type_id, BufferResource::ClassTypeId());
        EXPECT_EQ(events.front().reason, EvictionReason::kRefCountZero);
      }

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Subscribers receive only matching resource types.
/*!
 Scenario: Subscribe to texture evictions, then evict a buffer resource and
 verify no events are delivered to the texture subscriber.
*/
NOLINT_TEST_F(AssetLoaderEvictionAsyncTest, ResourceEviction_FiltersByType)
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

      const auto key = loader.MintSyntheticBufferKey();
      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(TextureResource::ClassTypeId(),
          [&](const EvictionEvent& event) { events.push_back(event); });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      loader.ReleaseResource(key);

      EXPECT_TRUE(events.empty());

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: ClearMounts emits eviction events with clear reason.
/*!
 Scenario: Cache a buffer resource, then clear mounts to drop the cache.
 Verify an eviction event is delivered with kClear reason.
*/
NOLINT_TEST_F(AssetLoaderEvictionAsyncTest, ResourceEviction_ClearMounts)
{
  using namespace std::chrono_literals;

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFill = 0x11;

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

      const auto key = loader.MintSyntheticBufferKey();
      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(BufferResource::ClassTypeId(),
          [&](const EvictionEvent& event) { events.push_back(event); });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      loader.ClearMounts();

      EXPECT_EQ(events.size(), 1u);
      if (!events.empty()) {
        EXPECT_EQ(events.front().reason, EvictionReason::kClear);
      }

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Stop emits eviction events with shutdown reason.
/*!
 Scenario: Cache a buffer resource, then stop the loader. Expect a shutdown
 eviction event to be delivered.
*/
NOLINT_TEST_F(AssetLoaderEvictionAsyncTest, ResourceEviction_Stop)
{
  using namespace std::chrono_literals;

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFill = 0x22;

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

      const auto key = loader.MintSyntheticBufferKey();
      auto bytes
        = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
      std::span<const uint8_t> span(bytes.data(), bytes.size());

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(BufferResource::ClassTypeId(),
          [&](const EvictionEvent& event) { events.push_back(event); });

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      loader.Stop();

      EXPECT_EQ(events.size(), 1u);
      if (!events.empty()) {
        EXPECT_EQ(events.front().reason, EvictionReason::kShutdown);
      }

      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Asset release cascades texture eviction events.
/*!
 Scenario: Load a material asset with texture dependencies, release the asset,
 and verify each texture dependency emits a refcount eviction event.
*/
NOLINT_TEST_F(
  AssetLoaderEvictionAsyncTest, AssetRelease_CascadesTextureEvictions)
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

      std::vector<EvictionEvent> events;
      auto subscription
        = loader.SubscribeResourceEvictions(TextureResource::ClassTypeId(),
          [&](const EvictionEvent& event) { events.push_back(event); });

      auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());
      material.reset();

      loader.ReleaseAsset(material_key);

      EXPECT_EQ(events.size(), 3u);
      for (const auto& event : events) {
        EXPECT_EQ(event.type_id, TextureResource::ClassTypeId());
        EXPECT_EQ(event.reason, EvictionReason::kRefCountZero);
      }

      std::unordered_set<std::size_t> unique_keys;
      for (const auto& event : events) {
        unique_keys.insert(std::hash<ResourceKey> {}(event.key));
      }
      EXPECT_EQ(unique_keys.size(), events.size());

      loader.Stop();
      (void)subscription;
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace
