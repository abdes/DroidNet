//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"
#include "Utils/PakUtils.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat_core.h>
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
using oxygen::content::ResourceKey;
using oxygen::content::testing::AssetLoaderBasicTest;

using oxygen::data::BufferResource;
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

//! Fixture for buffer-provided async load tests.
class AssetLoaderBufferFromBufferAsyncTest : public AssetLoaderBasicTest { };

NOLINT_TEST_F(AssetLoaderBufferFromBufferAsyncTest,
  CookedResourceRejectsUnknownAndWrongKindBeforeDecode)
{
  TestEventLoop loop;
  oxygen::co::Run(loop, [](TestEventLoop* event_loop) -> Co<> {
    auto& loop = *event_loop;
    oxygen::co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(Tag::Get(), config);
    auto decode_calls = 0;
    loader.RegisterLoader([&decode_calls](const oxygen::content::LoaderContext&)
                            -> std::unique_ptr<BufferResource> {
      ++decode_calls;
      return nullptr;
    });
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      const auto wrong_kind = loader.MintSyntheticTextureKey();
      const auto unknown = ResourceKey { std::numeric_limits<uint64_t>::max() };
      for (const auto key : { wrong_kind, unknown }) {
        const auto result = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> { .key = key, .bytes = {} });
        EXPECT_EQ(result, nullptr);
        EXPECT_FALSE(loader.HasBuffer(key));
      }
      EXPECT_EQ(decode_calls, 0);
      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  }(&loop));
}

//! Test: LoadResourceAsync(cooked) decodes and caches BufferResource.
/*!
 Scenario: Provide cooked bytes for a BufferResource and load it using
 `LoadResourceAsync<BufferResource>(CookedResourceData<...>)`. Verify the
 resource is returned
 and becomes available via `GetResource` under the provided key.
*/
NOLINT_TEST_F(AssetLoaderBufferFromBufferAsyncTest,
  LoadResourceFromBufferAsyncBufferResourceCachesDecodedResource)
{

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  static constexpr uint8_t kFill = 0xAB;

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(
    el, [](std::vector<uint8_t> bytes, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      AssetLoaderConfig config {};

      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

      AssetLoader loader(Tag::Get(), config);
      const auto key = loader.MintSyntheticBufferKey();

      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        // Act
        std::span<const uint8_t> span(bytes.data(), bytes.size());
        auto resource = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key,
            .bytes = span,
          });

        // Assert
        EXPECT_THAT(resource, NotNull());
        EXPECT_EQ(resource->GetDataSize(), kSizeBytes);
        EXPECT_EQ(resource->GetData().size(), kSizeBytes);
        EXPECT_THAT(
          resource->GetData(), ::testing::Each(static_cast<uint8_t>(kFill)));

        auto cached = loader.GetResource<BufferResource>(key);
        EXPECT_THAT(cached, NotNull());
        EXPECT_EQ(cached.get(), resource.get());

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(bytes, &el));
}

//! Test: StartLoadBuffer(cooked) invokes callback on owning thread.
/*!
 Scenario: Start a buffer-provided BufferResource load via
 `StartLoadBuffer(CookedResourceData<...>)` and verify the callback is
 invoked with a valid result on the owning thread.
*/
NOLINT_TEST_F(AssetLoaderBufferFromBufferAsyncTest,
  StartLoadResourceFromBufferBufferResourceInvokesCallback)
{

  // Arrange
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  static constexpr uint8_t kFill = 0x5A;

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(
    el, [](std::vector<uint8_t> bytes, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      AssetLoaderConfig config {};

      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

      AssetLoader loader(Tag::Get(), config);
      const auto key = loader.MintSyntheticBufferKey();

      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

      unsigned int callback_count = 0U;
      std::shared_ptr<BufferResource> loaded;
      std::thread::id callback_thread;

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        const auto owning_thread = std::this_thread::get_id();

        std::span<const uint8_t> span(bytes.data(), bytes.size());
        loader.StartLoadBuffer(
          CookedResourceData<BufferResource> {
            .key = key,
            .bytes = span,
          },
          [&](std::shared_ptr<BufferResource> resource) -> void {
            loaded = std::move(resource);
            callback_thread = std::this_thread::get_id();
            ++callback_count;
          });

        co_await loader.WaitForPendingLoadsAsync();
        EXPECT_EQ(callback_count, 1U);
        EXPECT_THAT(loaded, NotNull());
        if (loaded) {
          EXPECT_EQ(loaded->GetData().size(), kSizeBytes);
          EXPECT_THAT(loaded->GetData(), ::testing::Each(kFill));
        }
        EXPECT_EQ(callback_thread, owning_thread);

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(bytes, &el));
}

//! Test: LoadResourceAsync(cooked) decodes and caches TextureResource.
/*!
 Scenario: Provide cooked bytes for a TextureResource and load it using
 `LoadResourceAsync<TextureResource>(CookedResourceData<...>)`. Verify the
 resource is returned
 and becomes available via `GetResource` under the provided key.
*/
NOLINT_TEST_F(AssetLoaderBufferFromBufferAsyncTest,
  LoadResourceFromBufferAsyncTextureResourceCachesDecodedResource)
{

  // Arrange
  static constexpr std::size_t kDataOffset = 256;
  static constexpr uint32_t kPixelBytes = 287;
  static constexpr auto kFill = std::byte { 0x99 };

  oxygen::data::pak::core::TextureResourceDesc desc {};
  desc.data_offset = static_cast<uint64_t>(kDataOffset);
  desc.texture_type = 3; // TextureType::kTexture2D
  desc.compression_type = 0;
  static constexpr uint32_t kTextureWidth = 128U;
  desc.width = kTextureWidth;
  desc.height = 64;
  desc.depth = 1;
  desc.array_layers = 1;
  desc.mip_levels = 1;
  desc.format = 0;
  desc.alignment = kDataOffset;

  const auto payload
    = oxygen::content::testing::MakeV4TexturePayload(kPixelBytes, kFill);
  desc.size_bytes = static_cast<uint32_t>(payload.size());

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el,
    [](oxygen::data::pak::core::TextureResourceDesc desc,
      std::vector<uint8_t> payload, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      AssetLoaderConfig config {};

      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

      AssetLoader loader(Tag::Get(), config);

      loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);

      const auto key = loader.MintSyntheticTextureKey();
      std::vector<uint8_t> bytes(kDataOffset + payload.size(), 0);
      std::memcpy(bytes.data(), &desc, sizeof(desc));
      std::memcpy(std::span(bytes).subspan(kDataOffset).data(), payload.data(),
        payload.size());

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        std::span<const uint8_t> span(bytes.data(), bytes.size());
        auto resource = co_await loader.LoadResourceAsync<TextureResource>(
          CookedResourceData<TextureResource> {
            .key = key,
            .bytes = span,
          });

        EXPECT_THAT(resource, NotNull());
        EXPECT_EQ(resource->GetWidth(), 128U);
        EXPECT_EQ(resource->GetHeight(), 64U);
        EXPECT_EQ(resource->GetDepth(), 1U);
        EXPECT_EQ(resource->GetArrayLayers(), 1U);
        EXPECT_EQ(resource->GetMipCount(), 1U);
        EXPECT_EQ(resource->GetData().size(), kPixelBytes);
        EXPECT_THAT(resource->GetData(),
          ::testing::Each(std::to_integer<uint8_t>(kFill)));

        auto cached = loader.GetResource<TextureResource>(key);
        EXPECT_THAT(cached, NotNull());
        EXPECT_EQ(cached.get(), resource.get());

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(desc, payload, &el));
}

} // namespace
