//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
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
using oxygen::data::MaterialAsset;

// NOLINTBEGIN(*-magic-numbers)

namespace {

class AssetLoaderAutoTrimTest : public AssetLoaderLoadingTest { };

class AssetLoaderAutoTrimAsyncTest : public AssetLoaderLoadingTest {
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

NOLINT_TEST_F(
  AssetLoaderAutoTrimTest, ResidencyPolicyRoundTripExpectedToExposeState)
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
  EXPECT_EQ(queried.trim_attempts, 0U);
  EXPECT_EQ(queried.reclaimed_items, 0U);
  EXPECT_EQ(queried.reclaimed_bytes, 0U);
  EXPECT_EQ(queried.blocked_roots, 0U);
}

NOLINT_TEST_F(AssetLoaderAutoTrimTest, ResidencyPolicyZeroBudgetExpectedToThrow)
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

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  ResidencyAutoTrimOnOverBudgetExpectedToReclaimAndRecordTelemetry)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFillA = 0x41;
  constexpr uint8_t kFillB = 0x42;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes_a
    = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFillA);
  auto bytes_b
    = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFillB);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      const auto key_a = loader.MintSyntheticBufferKey();
      const auto key_b = loader.MintSyntheticBufferKey();
      EXPECT_NE(key_a, key_b);

      std::span<const uint8_t> span_a(bytes_a.data(), bytes_a.size());
      auto res_a = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_a, .bytes = span_a });
      EXPECT_THAT(res_a, NotNull());

      auto checkout_a = loader.CheckOutResource<BufferResource>(key_a);
      EXPECT_THAT(checkout_a, NotNull());
      checkout_a.reset();
      loader.ReleaseResource(key_a);
      loader.ReleaseResource(key_a);
      EXPECT_TRUE(loader.HasBuffer(key_a));

      std::span<const uint8_t> span_b(bytes_b.data(), bytes_b.size());
      auto res_b = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_b, .bytes = span_b });
      EXPECT_THAT(res_b, NotNull());
      EXPECT_TRUE(loader.HasBuffer(key_b));
      EXPECT_FALSE(loader.HasBuffer(key_a));

      const auto state = loader.QueryResidencyPolicyState();
      EXPECT_GE(state.trim_attempts, 1U);
      EXPECT_GE(state.reclaimed_items, 1U);
      EXPECT_GE(state.reclaimed_bytes, 1U);

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  ResidencyManualModeStorePressureExpectedToAvoidAutoTrim)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  constexpr uint8_t kFillA = 0x51;
  constexpr uint8_t kFillB = 0x52;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kManual,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes_a
    = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFillA);
  auto bytes_b
    = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, kFillB);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      const auto key_a = loader.MintSyntheticBufferKey();
      const auto key_b = loader.MintSyntheticBufferKey();
      EXPECT_NE(key_a, key_b);

      std::span<const uint8_t> span_a(bytes_a.data(), bytes_a.size());
      auto res_a = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_a, .bytes = span_a });
      EXPECT_THAT(res_a, NotNull());
      loader.ReleaseResource(key_a);
      EXPECT_TRUE(loader.HasBuffer(key_a));

      std::span<const uint8_t> span_b(bytes_b.data(), bytes_b.size());
      auto res_b = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_b, .bytes = span_b });
      EXPECT_THAT(res_b, NotNull());
      EXPECT_TRUE(loader.HasBuffer(key_a));
      EXPECT_FALSE(loader.HasBuffer(key_b));

      const auto state = loader.QueryResidencyPolicyState();
      EXPECT_EQ(
        state.policy.trim_mode, oxygen::content::ResidencyTrimMode::kManual);
      EXPECT_EQ(state.trim_attempts, 0U);
      EXPECT_EQ(state.reclaimed_items, 0U);
      EXPECT_EQ(state.reclaimed_bytes, 0U);

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  AutoTrimPolicySwitchRuntimeExpectedToGateBehavior)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kManual,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes_a = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x61);
  auto bytes_b = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x62);
  auto bytes_c = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x63);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      const auto key_a = loader.MintSyntheticBufferKey();
      const auto key_b = loader.MintSyntheticBufferKey();
      const auto key_c = loader.MintSyntheticBufferKey();

      auto res_a = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_a, .bytes = bytes_a });
      EXPECT_THAT(res_a, NotNull());
      loader.ReleaseResource(key_a);
      EXPECT_TRUE(loader.HasBuffer(key_a));

      auto res_b = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_b, .bytes = bytes_b });
      EXPECT_THAT(res_b, NotNull());
      EXPECT_TRUE(loader.HasBuffer(key_a));
      EXPECT_FALSE(loader.HasBuffer(key_b));
      EXPECT_EQ(loader.QueryResidencyPolicyState().trim_attempts, 0U);

      loader.SetResidencyPolicy(oxygen::content::ResidencyPolicy {
        .cache_budget_bytes = 1,
        .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
        .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
      });

      auto res_c = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_c, .bytes = bytes_c });
      EXPECT_THAT(res_c, NotNull());
      EXPECT_TRUE(loader.HasBuffer(key_c));
      EXPECT_FALSE(loader.HasBuffer(key_a));
      EXPECT_GE(loader.QueryResidencyPolicyState().trim_attempts, 1U);

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  AutoTrimTelemetryMonotonicExpectedAcrossPressureEvents)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      auto prev = loader.QueryResidencyPolicyState();
      for (uint32_t i = 0; i < 3; ++i) {
        const auto key = loader.MintSyntheticBufferKey();
        auto bytes = MakeBytesFromHexdump(
          hexdump, kDataOffset + kSizeBytes, static_cast<uint8_t>(0x70 + i));
        auto res = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key,
            .bytes = std::span<const uint8_t>(bytes.data(), bytes.size()),
          });
        EXPECT_THAT(res, NotNull());
        loader.ReleaseResource(key);
        loader.ReleaseResource(key);

        const auto cur = loader.QueryResidencyPolicyState();
        EXPECT_GE(cur.trim_attempts, prev.trim_attempts);
        EXPECT_GE(cur.reclaimed_items, prev.reclaimed_items);
        EXPECT_GE(cur.reclaimed_bytes, prev.reclaimed_bytes);
        prev = cur;
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  StoreFailureRetryAfterForcedTrimExpectedToCacheDecodedResource)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes_a = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x81);
  auto bytes_b = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x82);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      const auto key_a = loader.MintSyntheticBufferKey();
      const auto key_b = loader.MintSyntheticBufferKey();

      auto first = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_a, .bytes = bytes_a });
      EXPECT_THAT(first, NotNull());
      loader.ReleaseResource(key_a);
      EXPECT_TRUE(loader.HasBuffer(key_a));

      auto second = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_b, .bytes = bytes_b });
      EXPECT_THAT(second, NotNull());
      EXPECT_TRUE(loader.HasBuffer(key_b));
      EXPECT_FALSE(loader.HasBuffer(key_a));

      const auto state = loader.QueryResidencyPolicyState();
      EXPECT_GE(state.trim_attempts, 1U);

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  ManualTrimTelemetryExpectedToUpdateWithoutAutoMode)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kManual,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x91);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      const auto key = loader.MintSyntheticBufferKey();
      auto res = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key, .bytes = bytes });
      EXPECT_THAT(res, NotNull());
      loader.ReleaseResource(key);
      EXPECT_TRUE(loader.HasBuffer(key));

      const auto before = loader.QueryResidencyPolicyState();
      loader.TrimCache();
      const auto after = loader.QueryResidencyPolicyState();

      EXPECT_EQ(
        before.policy.trim_mode, oxygen::content::ResidencyTrimMode::kManual);
      EXPECT_GE(after.trim_attempts, before.trim_attempts + 1U);
      EXPECT_GE(after.reclaimed_items, before.reclaimed_items + 1U);
      EXPECT_GE(after.reclaimed_bytes, before.reclaimed_bytes + 1U);
      EXPECT_FALSE(loader.HasBuffer(key));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(
  AssetLoaderAutoTrimAsyncTest, PinnedResourceExpectedToBlockAutoTrimEviction)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes_a = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xA1);
  auto bytes_b = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xA2);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      const auto key_a = loader.MintSyntheticBufferKey();
      const auto key_b = loader.MintSyntheticBufferKey();
      auto res_a = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_a, .bytes = bytes_a });
      EXPECT_THAT(res_a, NotNull());
      EXPECT_TRUE(loader.PinResource(key_a));

      loader.ReleaseResource(key_a);
      EXPECT_TRUE(loader.HasBuffer(key_a));

      auto res_b = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key_b, .bytes = bytes_b });
      EXPECT_THAT(res_b, NotNull());

      EXPECT_TRUE(loader.HasBuffer(key_a));
      EXPECT_FALSE(loader.HasBuffer(key_b));
      EXPECT_TRUE(loader.UnpinResource(key_a));

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  PinnedAssetDependencyGraphExpectedToBlockAutoTrimBranchRelease)
{
  const auto pak_path = GeneratePakFile("material_with_textures");
  const auto material_key = CreateTestAssetKey("textured_material");

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  auto bytes = MakeBytesFromHexdump(hexdump, 256 + 192, 0xB1);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddPakFile(pak_path);

      auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());
      const auto tex_key
        = material ? material->GetBaseColorTextureKey() : ResourceKey {};
      EXPECT_TRUE(loader.PinAsset(material_key));
      material.reset();
      (void)loader.ReleaseAsset(material_key);
      EXPECT_TRUE(loader.HasMaterialAsset(material_key));

      const auto pressure_key = loader.MintSyntheticBufferKey();
      auto pressure = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> {
          .key = pressure_key,
          .bytes = std::span<const uint8_t>(bytes.data(), bytes.size()),
        });
      EXPECT_THAT(pressure, NotNull());

      EXPECT_TRUE(loader.HasMaterialAsset(material_key));
      EXPECT_FALSE(loader.HasBuffer(pressure_key));

      EXPECT_TRUE(loader.UnpinAsset(material_key));
      loader.TrimCache();
      EXPECT_FALSE(loader.HasMaterialAsset(material_key));
      if (tex_key.get() != 0U) {
        EXPECT_FALSE(loader.HasTexture(tex_key));
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  AutoTrimDoesNotBreakDependencySymmetryAfterRepeatedCycles)
{
  const auto pak_path = GeneratePakFile("material_with_textures");
  const auto material_key = CreateTestAssetKey("textured_material");

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
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

      for (int i = 0; i < 4; ++i) {
        auto material
          = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
        EXPECT_THAT(material, NotNull());
        material.reset();
        (void)loader.ReleaseAsset(material_key);
      }

      const auto state = loader.QueryResidencyPolicyState();
      EXPECT_GE(state.trim_attempts, 1U);

      auto replay = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(replay, NotNull());
      replay.reset();
      (void)loader.ReleaseAsset(material_key);

      loader.TrimCache();
      EXPECT_FALSE(loader.HasMaterialAsset(material_key));
      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  ResourcePipelinePressureCallbackExpectedToTriggerOnlyOnStorePressure)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 16,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xC1);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      const auto key = loader.MintSyntheticBufferKey();

      auto first = co_await loader.LoadResourceAsync<BufferResource>(
        CookedResourceData<BufferResource> { .key = key, .bytes = bytes });
      EXPECT_THAT(first, NotNull());
      const auto before = loader.QueryResidencyPolicyState();

      for (int i = 0; i < 5; ++i) {
        auto cached = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> { .key = key, .bytes = bytes });
        EXPECT_THAT(cached, NotNull());
      }

      const auto after = loader.QueryResidencyPolicyState();
      EXPECT_EQ(after.trim_attempts, before.trim_attempts);
      EXPECT_EQ(after.reclaimed_items, before.reclaimed_items);
      EXPECT_EQ(after.reclaimed_bytes, before.reclaimed_bytes);

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderAutoTrimTest,
  PolicyRoundTripIncludesTelemetryExpectedStableAfterNoopOperations)
{
  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 4096,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  AssetLoader loader(
    oxygen::content::internal::EngineTagFactory::Get(), config);
  const auto s1 = loader.QueryResidencyPolicyState();
  const auto s2 = loader.QueryResidencyPolicyState();
  EXPECT_EQ(s1.policy.cache_budget_bytes, s2.policy.cache_budget_bytes);
  EXPECT_EQ(s1.policy.trim_mode, s2.policy.trim_mode);
  EXPECT_EQ(s1.policy.default_priority_class, s2.policy.default_priority_class);
  EXPECT_EQ(s1.trim_attempts, s2.trim_attempts);
  EXPECT_EQ(s1.reclaimed_items, s2.reclaimed_items);
  EXPECT_EQ(s1.reclaimed_bytes, s2.reclaimed_bytes);
  EXPECT_EQ(s1.blocked_roots, s2.blocked_roots);
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  CookedResourceManualVsAutoPressureExpectedDifferentResidencyOutcomes)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr std::size_t kDataOffset = 256;
  constexpr std::size_t kSizeBytes = 192;
  auto bytes_a = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xD1);
  auto bytes_b = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xD2);

  auto run_case = [&](const oxygen::content::ResidencyTrimMode mode) {
    AssetLoaderConfig config {};
    config.residency_policy = oxygen::content::ResidencyPolicy {
      .cache_budget_bytes = 1,
      .trim_mode = mode,
      .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
    };

    TestEventLoop el;
    bool has_first_after_pressure = false;
    bool has_second_after_pressure = false;
    uint64_t trim_attempts = 0;

    (oxygen::co::Run)(el, [&]() -> Co<> {
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(
        oxygen::content::internal::EngineTagFactory::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        const auto key_a = loader.MintSyntheticBufferKey();
        const auto key_b = loader.MintSyntheticBufferKey();

        auto a = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_a, .bytes = bytes_a });
        EXPECT_THAT(a, NotNull());
        loader.ReleaseResource(key_a);

        auto b = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_b, .bytes = bytes_b });
        EXPECT_THAT(b, NotNull());

        has_first_after_pressure = loader.HasBuffer(key_a);
        has_second_after_pressure = loader.HasBuffer(key_b);
        trim_attempts = loader.QueryResidencyPolicyState().trim_attempts;
        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    });

    return std::tuple { has_first_after_pressure, has_second_after_pressure,
      trim_attempts };
  };

  const auto [manual_first, manual_second, manual_trim]
    = run_case(oxygen::content::ResidencyTrimMode::kManual);
  const auto [auto_first, auto_second, auto_trim]
    = run_case(oxygen::content::ResidencyTrimMode::kAutoOnOverBudget);

  EXPECT_TRUE(manual_first);
  EXPECT_FALSE(manual_second);
  EXPECT_EQ(manual_trim, 0U);

  EXPECT_FALSE(auto_first);
  EXPECT_TRUE(auto_second);
  EXPECT_GE(auto_trim, 1U);
}

} // namespace

// NOLINTEND(*-magic-numbers)
