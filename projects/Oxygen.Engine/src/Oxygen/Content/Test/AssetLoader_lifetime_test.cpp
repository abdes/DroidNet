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
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"
#include "Utils/PakUtils.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Internal/InFlightOperationTable.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#ifndef NDEBUG
#  include <Oxygen/Data/AssetKey.h>
#endif
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Shared.h>
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
using oxygen::content::testing::AssetLoaderLoadingTest;

using oxygen::data::BufferResource;
using oxygen::data::GeometryAsset;
using oxygen::data::InputMappingContextAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

// NOLINTBEGIN(*-magic-numbers)

using oxygen::base::CheckedAt;

namespace {

//=== AssetLoader Lifetime Tests ===-----------------------------------------//

class AssetLoaderLifetimeTest : public AssetLoaderLoadingTest { };

class AssetLoaderLifetimeAsyncTest : public AssetLoaderLoadingTest {
protected:
  void SetUp() override
  {
    AssetLoaderLoadingTest::SetUp();
    asset_loader_.reset();
  }
};

auto MakeBytesFromHexdump(const std::string& hexdump, const std::size_t size,
  const uint8_t fill) -> std::vector<uint8_t>
{
  auto header = oxygen::content::testing::ParseHexDumpWithOffset(
    hexdump, static_cast<int>(size), std::byte { fill });

  std::vector<uint8_t> bytes(size, fill);
  const auto copy_count = std::min(bytes.size(), header.size());
  for (std::size_t i = 0; i < copy_count; ++i) {
    bytes.at(i) = static_cast<uint8_t>(header.at(i));
  }

  return bytes;
}

//! Owner destruction returns usage; explicit trim removes idle cache entries.
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest, ResourceOwnerDropAllowsTrim)
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

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
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

      std::span<const uint8_t> span(bytes.data(), bytes.size());
      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });

      EXPECT_THAT(resource, NotNull());
      resource.reset();

      EXPECT_TRUE(loader.HasBuffer(key));
      auto cached = loader.GetBuffer(key);
      EXPECT_THAT(cached, NotNull());
      cached.reset();

      resource.reset();
      EXPECT_TRUE(loader.HasBuffer(key));
      loader.TrimCache();
      EXPECT_FALSE(loader.HasBuffer(key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Characterization: default residency is effectively unbounded and
//! trim-driven.
/*!
 Scenario: Load many synthetic buffers into the runtime cache using cooked
 payloads. Release all checkouts and verify entries remain present until an
 explicit TrimCache call. This captures the current manual-trim residency model.
*/
NOLINT_TEST_F(
  AssetLoaderLifetimeAsyncTest, Characterization_DefaultResidencyIsManualTrim)
{
  constexpr int kResourceCount = 96;
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFill = 0x7DU;
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
  std::span<const uint8_t> span(bytes.data(), bytes.size());

  TestEventLoop el;
  oxygen::co::Run(el, [&] -> Co<> {
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(Tag::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    std::vector<ResourceKey> loaded_keys;
    loaded_keys.reserve(kResourceCount);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      for (int i = 0; i < kResourceCount; ++i) {
        const auto key = loader.MintSyntheticBufferKey();
        auto resource = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key,
            .bytes = span,
          });
        EXPECT_THAT(resource, NotNull());
        resource.reset();
        EXPECT_TRUE(loader.HasBuffer(key));
        loaded_keys.push_back(key);
      }

      for (const auto key : loaded_keys) {
        EXPECT_TRUE(loader.HasBuffer(key));
      }

      loader.TrimCache();
      for (const auto key : loaded_keys) {
        EXPECT_FALSE(loader.HasBuffer(key));
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Independent acquisitions keep an entry resident until their last owners
//! drop.
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest, ResourceUnloadRefcountedCheckouts)
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

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
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

      std::span<const uint8_t> span(bytes.data(), bytes.size());
      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());

      auto extra_checkout = loader.GetResource<BufferResource>(key);
      EXPECT_THAT(extra_checkout, NotNull());
      extra_checkout.reset();

      resource.reset();
      EXPECT_TRUE(loader.HasBuffer(key));

      resource.reset();
      EXPECT_TRUE(loader.HasBuffer(key));
      loader.TrimCache();
      EXPECT_FALSE(loader.HasBuffer(key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: explicit resource pin/unpin requires symmetry and preserves residency.
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest, ResourcePinUnpinSymmetryExpected)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFill = 0x31;
  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

  TestEventLoop el;
  oxygen::co::Run(el, [&] -> Co<> {
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);
    const auto key = loader.MintSyntheticBufferKey();
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      std::span<const uint8_t> span(bytes.data(), bytes.size());
      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());
      resource.reset();

      auto residency_pin = loader.PinResource(key);
      EXPECT_TRUE(residency_pin);
      resource.reset();
      EXPECT_TRUE(loader.HasBuffer(key));

      residency_pin.Reset();
      EXPECT_FALSE(residency_pin);
      residency_pin.Reset();
      EXPECT_FALSE(residency_pin);

      loader.TrimCache();
      EXPECT_FALSE(loader.HasBuffer(key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST(InFlightOperationTableLifetimeTest, CleanupCanReenterRetirement)
{
  using Table = oxygen::content::internal::InFlightOperationTable;
  enum class Retirement : uint8_t { kErase, kClear, kDestruction };
  const auto hold
    = [](auto cleanup) -> Co<oxygen::content::internal::SharedContentResult> {
    static_cast<void>(cleanup);
    co_return oxygen::content::internal::SharedContentResult {};
  };
  for (const auto mode :
    { Retirement::kErase, Retirement::kClear, Retirement::kDestruction }) {
    size_t retired = 0U;
    {
      Table table;
      const auto type = oxygen::data::BufferResource::ClassTypeId();
      constexpr uint64_t kHash = 1U;
      const auto id = table.NewOperationId();
      const auto cleanup = [&table, type, id, &retired]() noexcept -> void {
        ++retired;
        table.Erase(type, kHash, id);
      };
      auto guard
        = std::make_unique<oxygen::ScopeGuard<decltype(cleanup)>>(cleanup);
      table.Insert(
        type, kHash, id, oxygen::co::Shared(hold(std::move(guard))), {});
      if (mode == Retirement::kErase) {
        table.Erase(type, kHash, id);
      } else if (mode == Retirement::kClear) {
        table.Clear();
      }
    }
    EXPECT_EQ(retired, 1U);
  }
}

NOLINT_TEST(
  InFlightOperationTableLifetimeTest, LateCleanupCannotEraseReplacement)
{
  using Table = oxygen::content::internal::InFlightOperationTable;
  Table table;
  const auto type = oxygen::data::BufferResource::ClassTypeId();
  constexpr uint64_t kHash = 1U;
  const auto old_id = table.NewOperationId();
  const auto cleanup = [&table, type, old_id]() noexcept -> void {
    table.Erase(type, kHash, old_id);
  };
  const auto hold
    = [](auto guard) -> Co<oxygen::content::internal::SharedContentResult> {
    static_cast<void>(guard);
    co_return oxygen::content::internal::SharedContentResult {};
  };
  auto old = oxygen::co::Shared(
    hold(std::make_unique<oxygen::ScopeGuard<decltype(cleanup)>>(cleanup)));
  table.Insert(type, kHash, old_id, old, {});
  table.Clear();
  const auto replacement_id = table.NewOperationId();
  auto replacement = oxygen::co::Shared(
    []() -> Co<oxygen::content::internal::SharedContentResult> {
      co_return oxygen::content::internal::SharedContentResult {};
    }());
  table.Insert(type, kHash, replacement_id, replacement, {});
  old = {};
  EXPECT_TRUE(table.GetRequestMeta(type, kHash).has_value());
  table.Close();
  EXPECT_THROW(static_cast<void>(table.NewOperationId()),
    oxygen::content::OperationCancelledException);
}

NOLINT_TEST(
  InFlightOperationTablePriorityContractTest, HigherPriorityPromotesMetadata)
{
  oxygen::content::internal::InFlightOperationTable table {};
  const auto kTypeId = oxygen::data::BufferResource::ClassTypeId();
  constexpr uint64_t kHash = 0xABU;
  auto op = oxygen::co::Shared(
    [] -> Co<oxygen::content::internal::SharedContentResult> {
      co_return oxygen::content::internal::SharedContentResult {};
    }());

  table.Insert(kTypeId, kHash, table.NewOperationId(), op,
    {
      .priority = oxygen::content::LoadPriority::kDefault,
      .intent = oxygen::content::LoadIntent::kRuntime,
      .sequence = 10U,
    });

  auto joined = table.Find(kTypeId, kHash,
    {
      .priority = oxygen::content::LoadPriority::kCritical,
      .intent = oxygen::content::LoadIntent::kStreaming,
      .sequence = 20U,
    });
  EXPECT_TRUE(joined.has_value());

  const auto meta = table.GetRequestMeta(kTypeId, kHash);
  if (!meta.has_value()) {
    FAIL() << "Expected meta to contain a value";
  }
  EXPECT_EQ(meta->priority, oxygen::content::LoadPriority::kCritical);
  EXPECT_EQ(meta->intent, oxygen::content::LoadIntent::kStreaming);
  EXPECT_EQ(meta->sequence, 10U);
}

NOLINT_TEST(InFlightOperationTablePriorityContractTest, TieUsesEarliestSequence)
{
  oxygen::content::internal::InFlightOperationTable table {};
  const auto kTypeId = oxygen::data::BufferResource::ClassTypeId();
  constexpr uint64_t kHash = 0xCDU;
  auto op = oxygen::co::Shared(
    [] -> Co<oxygen::content::internal::SharedContentResult> {
      co_return oxygen::content::internal::SharedContentResult {};
    }());

  table.Insert(kTypeId, kHash, table.NewOperationId(), op,
    {
      .priority = oxygen::content::LoadPriority::kDefault,
      .intent = oxygen::content::LoadIntent::kRuntime,
      .sequence = 5U,
    });

  (void)table.Find(kTypeId, kHash,
    {
      .priority = oxygen::content::LoadPriority::kDefault,
      .intent = oxygen::content::LoadIntent::kStreaming,
      .sequence = 9U,
    });
  auto meta = table.GetRequestMeta(kTypeId, kHash);
  if (!meta.has_value()) {
    FAIL() << "Expected meta to contain a value";
  }
  EXPECT_EQ(meta->sequence, 5U);
  EXPECT_EQ(meta->intent, oxygen::content::LoadIntent::kRuntime);

  (void)table.Find(kTypeId, kHash,
    {
      .priority = oxygen::content::LoadPriority::kDefault,
      .intent = oxygen::content::LoadIntent::kPrewarm,
      .sequence = 3U,
    });
  meta = table.GetRequestMeta(kTypeId, kHash);
  if (!meta.has_value()) {
    FAIL() << "Expected meta to contain a value";
  }
  EXPECT_EQ(meta->sequence, 3U);
  EXPECT_EQ(meta->intent, oxygen::content::LoadIntent::kPrewarm);
}

NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest,
  LoadRequestForCookedResourceExpectedToPropagateToLoaderContext)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFill = 0x19;
  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
  std::span<const uint8_t> span(bytes.data(), bytes.size());

  TestEventLoop el;
  oxygen::co::Run(el, [&] -> Co<> {
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);
    const auto key = loader.MintSyntheticBufferKey();

    auto seen_priority = oxygen::content::LoadPriority::kDefault;
    auto seen_intent = oxygen::content::LoadIntent::kRuntime;
    loader.RegisterLoader([&seen_priority, &seen_intent](
                            const oxygen::content::LoaderContext& context)
                            -> std::unique_ptr<oxygen::data::BufferResource> {
      seen_priority = context.request_priority;
      seen_intent = context.request_intent;
      return oxygen::content::loaders::LoadBufferResource(context);
    });

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        },
        oxygen::content::LoadRequest {
          .priority = oxygen::content::LoadPriority::kCritical,
          .intent = oxygen::content::LoadIntent::kStreaming,
        });
      EXPECT_THAT(resource, NotNull());
      EXPECT_EQ(seen_priority, oxygen::content::LoadPriority::kCritical);
      EXPECT_EQ(seen_intent, oxygen::content::LoadIntent::kStreaming);

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Idle assets remain reusable until the caller explicitly trims the cache.
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest, AssetOwnerDropAllowsTrim)
{
  using namespace std::chrono_literals;

  // Arrange
  const auto pak_path = GeneratePakFile("material_with_textures");
  const auto material_key = CreateTestAssetKey("textured_material");

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
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

      auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());
      const auto* first_ptr = material.get();
      material.reset();

      EXPECT_TRUE(loader.HasMaterialAsset(material_key));

      auto cached = loader.GetMaterialAsset(material_key);
      EXPECT_THAT(cached, NotNull());
      EXPECT_EQ(cached.get(), first_ptr);
      cached.reset();

      EXPECT_TRUE(loader.HasMaterialAsset(material_key));
      loader.TrimCache();
      EXPECT_FALSE(loader.HasMaterialAsset(material_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: pinned asset keeps dependency release traversal deferred until unpin.
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest,
  AssetPinReleaseTraversalExpectedToPreserveDependenciesUntilUnpin)
{
  const auto pak_path = GeneratePakFile("material_with_textures");
  const auto material_key = CreateTestAssetKey("textured_material");

  TestEventLoop el;
  oxygen::co::Run(el, [&] -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddPakFile(pak_path);

      auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());
      const auto texture_key
        = material ? material->GetBaseColorTextureKey() : ResourceKey {};
      EXPECT_TRUE(texture_key.get() != 0U);
      EXPECT_TRUE(loader.HasTexture(texture_key));

      auto residency_pin = loader.PinAsset(material_key);
      EXPECT_TRUE(residency_pin);

      material.reset();

      EXPECT_TRUE(loader.HasMaterialAsset(material_key));
      EXPECT_TRUE(loader.HasTexture(texture_key));

      residency_pin.Reset();
      EXPECT_FALSE(residency_pin);
      residency_pin.Reset();
      EXPECT_FALSE(residency_pin);

      loader.TrimCache();
      EXPECT_FALSE(loader.HasMaterialAsset(material_key));
      EXPECT_FALSE(loader.HasTexture(texture_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

#ifndef NDEBUG

#endif

//! Test: async geometry load binds dependencies and unloads after trim.
/*!
 Scenario: Load a geometry asset that references buffers and material assets.
 Verify the geometry is returned and releasing the asset drops usage; TrimCache
 performs deterministic eviction.
*/
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest,
  LoadAssetAsyncGeometryWithBuffersBindsDependenciesAndUnloadsInOrder)
{
  using namespace std::chrono_literals;

  // Arrange
  const auto pak_path = GeneratePakFile("geometry_with_buffers");
  const auto geometry_key = CreateTestAssetKey("buffered_geometry");

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      auto geometry
        = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
      EXPECT_THAT(geometry, NotNull());

      if (geometry) {
        const auto meshes = geometry->Meshes();
        EXPECT_FALSE(meshes.empty());

        if (!meshes.empty() && CheckedAt(meshes, 0)) {
          EXPECT_EQ(CheckedAt(meshes, 0)->VertexCount(), 6U);
          EXPECT_EQ(CheckedAt(meshes, 0)->IndexCount(), 3U);

          const auto submeshes = CheckedAt(meshes, 0)->SubMeshes();
          EXPECT_FALSE(submeshes.empty());
          if (!submeshes.empty()) {
            EXPECT_THAT(CheckedAt(submeshes, 0).Material(), NotNull());
          }
        }
      }

      geometry.reset();

      EXPECT_TRUE(loader.HasGeometryAsset(geometry_key));
      loader.TrimCache();
      EXPECT_FALSE(loader.HasGeometryAsset(geometry_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace

// NOLINTEND(*-magic-numbers)
