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
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Internal/InFlightOperationTable.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/InputActionLoader.h>
#include <Oxygen/Content/Loaders/InputMappingContextLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Shared.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

#include "./AssetLoader_test.h"
#include "Utils/PakUtils.h"

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
    bytes[i] = static_cast<uint8_t>(header[i]);
  }

  return bytes;
}

//! Test: Resource remains cached until explicit release and trim.
/*!
 Scenario: Load a BufferResource from cooked bytes, drop the returned
 shared_ptr, and verify the resource is still cached. Only after calling
 ReleaseResource and TrimCache should the cache entry be evicted.
*/
NOLINT_TEST_F(
  AssetLoaderLifetimeAsyncTest, ResourceUnloadRequiresExplicitRelease)
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

  const auto key = ResourceKey { 0xABCDEFU };
  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

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

      loader.ReleaseResource(key);
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
  constexpr uint32_t kResourceKeyBase = 0xA0B00000U;
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
  (oxygen::co::Run)(el, [&]() -> Co<> {
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };

    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    std::vector<ResourceKey> loaded_keys;
    loaded_keys.reserve(kResourceCount);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      for (int i = 0; i < kResourceCount; ++i) {
        const auto key = ResourceKey {
          static_cast<uint64_t>(kResourceKeyBase + static_cast<uint32_t>(i)),
        };
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
        (void)loader.ReleaseResource(key);
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

//! Test: Runtime residency policy round-trip and state query.
NOLINT_TEST_F(
  AssetLoaderLifetimeTest, ResidencyPolicyRoundTripExpectedToExposeState)
{
  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 4096,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kCritical,
  };

  AssetLoader loader(
    oxygen::content::internal::EngineTagFactory::Get(), config);

  const auto configured = loader.GetResidencyPolicy();
  EXPECT_EQ(configured.cache_budget_bytes, 4096U);
  EXPECT_EQ(configured.trim_mode,
    oxygen::content::ResidencyTrimMode::kAutoOnOverBudget);
  EXPECT_EQ(configured.default_priority_class,
    oxygen::content::LoadPriorityClass::kCritical);

  const auto queried = loader.QueryResidencyPolicyState();
  EXPECT_EQ(queried.policy.cache_budget_bytes, 4096U);
  EXPECT_EQ(queried.policy.trim_mode,
    oxygen::content::ResidencyTrimMode::kAutoOnOverBudget);
  EXPECT_EQ(queried.policy.default_priority_class,
    oxygen::content::LoadPriorityClass::kCritical);
  EXPECT_EQ(queried.cache_entries, 0U);
  EXPECT_EQ(queried.consumed_bytes, 0U);
  EXPECT_EQ(queried.checked_out_items, 0U);
  EXPECT_FALSE(queried.over_budget);
}

//! Test: Residency policy rejects zero cache budget.
NOLINT_TEST_F(AssetLoaderLifetimeTest, ResidencyPolicyZeroBudgetExpectedToThrow)
{
  AssetLoaderConfig config {};
  AssetLoader loader(
    oxygen::content::internal::EngineTagFactory::Get(), config);

  const auto original = loader.GetResidencyPolicy();
  EXPECT_GT(original.cache_budget_bytes, 0U);

  EXPECT_THROW(
    loader.SetResidencyPolicy(oxygen::content::ResidencyPolicy {
      .cache_budget_bytes = 0,
      .trim_mode = oxygen::content::ResidencyTrimMode::kManual,
      .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
    }),
    std::invalid_argument);

  const auto after = loader.GetResidencyPolicy();
  EXPECT_EQ(after.cache_budget_bytes, original.cache_budget_bytes);
  EXPECT_EQ(after.trim_mode, original.trim_mode);
  EXPECT_EQ(after.default_priority_class, original.default_priority_class);
}

//! Test: Refcounted checkouts require matching releases and trim.
/*!
 Scenario: Load a BufferResource, check it out once more using GetBuffer,
 and verify that a single ReleaseResource does not evict the entry. A second
 ReleaseResource still leaves the cache baseline; TrimCache evicts it.
*/
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

  const auto key = ResourceKey { 0x12345678U };
  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

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

      std::span<const uint8_t> span(bytes.data(), bytes.size());
      auto resource = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = key,
          .bytes = span,
        });
      EXPECT_THAT(resource, NotNull());

      auto extra_checkout = loader.CheckOutResource<BufferResource>(key);
      EXPECT_THAT(extra_checkout, NotNull());
      extra_checkout.reset();

      loader.ReleaseResource(key);
      EXPECT_TRUE(loader.HasBuffer(key));

      loader.ReleaseResource(key);
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
  const auto key = ResourceKey { 0x00ABCD12U };
  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
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

      EXPECT_TRUE(loader.PinResource(key));
      (void)loader.ReleaseResource(key);
      EXPECT_TRUE(loader.HasBuffer(key));

      EXPECT_TRUE(loader.UnpinResource(key));
      EXPECT_FALSE(loader.UnpinResource(key));

      loader.TrimCache();
      EXPECT_FALSE(loader.HasBuffer(key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST(
  InFlightOperationTablePriorityContractTest, HigherPriorityPromotesMetadata)
{
  oxygen::content::internal::InFlightOperationTable table {};
  const auto kTypeId = oxygen::data::BufferResource::ClassTypeId();
  constexpr uint64_t kHash = 0xABU;
  auto op = oxygen::co::Shared(
    []() -> Co<std::shared_ptr<void>> { co_return nullptr; }());

  table.InsertOrAssign(kTypeId, kHash, op,
    { .priority = oxygen::content::LoadPriority::kDefault,
      .intent = oxygen::content::LoadIntent::kRuntime,
      .sequence = 10U });

  auto joined = table.Find(kTypeId, kHash,
    { .priority = oxygen::content::LoadPriority::kCritical,
      .intent = oxygen::content::LoadIntent::kStreaming,
      .sequence = 20U });
  EXPECT_TRUE(joined.has_value());

  const auto meta = table.GetRequestMeta(kTypeId, kHash);
  ASSERT_TRUE(meta.has_value());
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
    []() -> Co<std::shared_ptr<void>> { co_return nullptr; }());

  table.InsertOrAssign(kTypeId, kHash, op,
    { .priority = oxygen::content::LoadPriority::kDefault,
      .intent = oxygen::content::LoadIntent::kRuntime,
      .sequence = 5U });

  (void)table.Find(kTypeId, kHash,
    { .priority = oxygen::content::LoadPriority::kDefault,
      .intent = oxygen::content::LoadIntent::kStreaming,
      .sequence = 9U });
  auto meta = table.GetRequestMeta(kTypeId, kHash);
  ASSERT_TRUE(meta.has_value());
  EXPECT_EQ(meta->sequence, 5U);
  EXPECT_EQ(meta->intent, oxygen::content::LoadIntent::kRuntime);

  (void)table.Find(kTypeId, kHash,
    { .priority = oxygen::content::LoadPriority::kDefault,
      .intent = oxygen::content::LoadIntent::kPrewarm,
      .sequence = 3U });
  meta = table.GetRequestMeta(kTypeId, kHash);
  ASSERT_TRUE(meta.has_value());
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
  const auto key = ResourceKey { 0x00FEED12U };
  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFill);
  std::span<const uint8_t> span(bytes.data(), bytes.size());

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    AssetLoaderConfig config {};
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

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

//! Test: Asset remains cached and is reused until explicit release and trim.
/*!
 Scenario: Load a MaterialAsset, drop the returned shared_ptr without calling
 ReleaseAsset, then load again and expect the same cached instance. After
 ReleaseAsset, the entry remains as cache baseline; TrimCache removes it.
*/
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest, AssetUnloadRequiresExplicitRelease)
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

      (void)loader.ReleaseAsset(material_key);
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
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

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

      EXPECT_TRUE(loader.PinAsset(material_key));

      material.reset();
      (void)loader.ReleaseAsset(material_key);
      EXPECT_TRUE(loader.HasMaterialAsset(material_key));
      EXPECT_TRUE(loader.HasTexture(texture_key));

      EXPECT_TRUE(loader.UnpinAsset(material_key));
      EXPECT_FALSE(loader.UnpinAsset(material_key));

      loader.TrimCache();
      EXPECT_FALSE(loader.HasMaterialAsset(material_key));
      EXPECT_FALSE(loader.HasTexture(texture_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Release order unloads dependency before dependent.
/*!
 Scenario: A depends on B. Releasing A cascades and causes B to be checked in
 before A.
*/
NOLINT_TEST_F(AssetLoaderLifetimeTest, ReleaseOrderDependencyBeforeDependent)
{
  // Arrange
  const auto key_a = CreateTestAssetKey("release_a");
  const auto key_b = CreateTestAssetKey("release_b");
  asset_loader_->AddAssetDependency(key_a, key_b);

  // Act
  asset_loader_->ReleaseAsset(key_a);
  asset_loader_->ReleaseAsset(key_b);

  // Assert (idempotence)
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_a));
  EXPECT_TRUE(asset_loader_->ReleaseAsset(key_b));
}

#if !defined(NDEBUG)
//! Test: Releasing one of multiple dependents does not evict shared dependency.
/*!
 Scenario: A -> C, B -> C. Release A; C must remain for B. Then release B; C
 may be released.
*/
NOLINT_TEST_F(
  AssetLoaderLifetimeTest, CascadeReleaseSiblingSharedDependencyNotEvicted)
{
  const auto key_a = CreateTestAssetKey("cascade_a");
  const auto key_b = CreateTestAssetKey("cascade_b");
  const auto key_c = CreateTestAssetKey("cascade_shared");
  asset_loader_->AddAssetDependency(key_a, key_c);
  asset_loader_->AddAssetDependency(key_b, key_c);

  size_t dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const oxygen::data::AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 2);

  asset_loader_->ReleaseAsset(key_a);

  dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const oxygen::data::AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 1);

  asset_loader_->ReleaseAsset(key_b);

  dependents_of_c = 0;
  asset_loader_->ForEachDependent(
    key_c, [&](const oxygen::data::AssetKey&) { ++dependents_of_c; });
  EXPECT_EQ(dependents_of_c, 0);

  asset_loader_->ReleaseAsset(key_a);
  asset_loader_->ReleaseAsset(key_b);
}
#endif // !NDEBUG

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
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

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

        if (!meshes.empty() && meshes[0]) {
          EXPECT_EQ(meshes[0]->VertexCount(), 6U);
          EXPECT_EQ(meshes[0]->IndexCount(), 3U);

          const auto submeshes = meshes[0]->SubMeshes();
          EXPECT_FALSE(submeshes.empty());
          if (!submeshes.empty()) {
            EXPECT_THAT(submeshes[0].Material(), NotNull());
          }
        }
      }

      geometry.reset();
      (void)loader.ReleaseAsset(geometry_key);
      EXPECT_TRUE(loader.HasGeometryAsset(geometry_key));
      loader.TrimCache();
      EXPECT_FALSE(loader.HasGeometryAsset(geometry_key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Scene->InputMappingContext->InputAction chain lifetime.
/*!
 Scenario: Load a scene with input context bindings and verify dependency
 * chain:
 scene depends on input mapping context; context depends on actions.

 * Releasing scene should not immediately evict context/actions until they are

 * explicitly released and cache is trimmed.
*/
NOLINT_TEST_F(AssetLoaderLifetimeAsyncTest,
  LoadAssetAsyncSceneInputChainReleaseOrderAndTrim)
{
  const auto pak_path = GeneratePakFile("scene_with_input_context_binding");
  const auto scene_key = CreateTestAssetKey("test_scene_with_input_context");
  const auto context_key = CreateTestAssetKey("test_input_mapping_context");
  const auto action_a = CreateTestAssetKey("test_input_action_accelerate");
  const auto action_b = CreateTestAssetKey("test_input_action_decelerate");

  TestEventLoop el;

  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadInputActionAsset);
    loader.RegisterLoader(
      oxygen::content::loaders::LoadInputMappingContextAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      auto scene
        = co_await loader.LoadAssetAsync<oxygen::data::SceneAsset>(scene_key);
      EXPECT_THAT(scene, NotNull());
      if (!scene) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      auto context
        = co_await loader.LoadAssetAsync<InputMappingContextAsset>(context_key);
      EXPECT_THAT(context, NotNull());
      if (!context) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }

      EXPECT_TRUE(loader.HasAsset<oxygen::data::SceneAsset>(scene_key));
      EXPECT_TRUE(loader.HasAsset<InputMappingContextAsset>(context_key));
      EXPECT_TRUE(loader.HasAsset<oxygen::data::InputActionAsset>(action_a));
      EXPECT_TRUE(loader.HasAsset<oxygen::data::InputActionAsset>(action_b));

      scene.reset();
      (void)loader.ReleaseAsset(scene_key);
      loader.TrimCache();

      // context/actions remain because explicitly checked out and/or linked.
      EXPECT_TRUE(loader.HasAsset<InputMappingContextAsset>(context_key));
      EXPECT_TRUE(loader.HasAsset<oxygen::data::InputActionAsset>(action_a));
      EXPECT_TRUE(loader.HasAsset<oxygen::data::InputActionAsset>(action_b));

      context.reset();
      (void)loader.ReleaseAsset(context_key);
      (void)loader.ReleaseAsset(action_a);
      (void)loader.ReleaseAsset(action_b);
      loader.TrimCache();

      EXPECT_FALSE(loader.HasAsset<oxygen::data::SceneAsset>(scene_key));
      EXPECT_FALSE(loader.HasAsset<InputMappingContextAsset>(context_key));
      EXPECT_FALSE(loader.HasAsset<oxygen::data::InputActionAsset>(action_a));
      EXPECT_FALSE(loader.HasAsset<oxygen::data::InputActionAsset>(action_b));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace

// NOLINTEND(*-magic-numbers)
