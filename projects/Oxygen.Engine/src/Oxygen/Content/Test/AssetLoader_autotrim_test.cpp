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
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"
#include "Utils/PakUtils.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/MaterialAsset.h>
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
using oxygen::data::MaterialAsset;

// NOLINTBEGIN(*-magic-numbers)

namespace {

class AssetLoaderAutoTrimTest : public AssetLoaderBasicTest { };

class AssetLoaderAutoTrimAsyncTest : public AssetLoaderBasicTest {
protected:
  void SetUp() override
  {
    AssetLoaderBasicTest::SetUp();
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

NOLINT_TEST_F(
  AssetLoaderAutoTrimTest, ResidencyPolicyRoundTripExpectedToExposeState)
{
  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 4096,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kCritical,
  };

  AssetLoader loader(Tag::Get(), config);

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
  AssetLoader loader(Tag::Get(), config);

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
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  static constexpr uint8_t kFillA = 0x41;
  static constexpr uint8_t kFillB = 0x42;

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
  oxygen::co::Run(el,
    [](AssetLoaderConfig config, std::vector<uint8_t> bytes_a,
      std::vector<uint8_t> bytes_b, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
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

        auto checkout_a = loader.GetResource<BufferResource>(key_a);
        EXPECT_THAT(checkout_a, NotNull());
        checkout_a.reset();
        res_a.reset();
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
        const auto telemetry = loader.GetTelemetryStats();
        EXPECT_GE(telemetry.buffer_resources.at(
                    AssetLoader::TypedLoadMetric::kRequests),
          2U);
        EXPECT_GE(telemetry.buffer_resources.at(
                    AssetLoader::TypedLoadMetric::kCacheMisses),
          2U);
        EXPECT_GE(telemetry.buffer_resources.at(
                    AssetLoader::TypedLoadMetric::kTasksSpawned),
          2U);
        EXPECT_EQ(telemetry.trim.manual_attempts + telemetry.trim.auto_attempts,
          state.trim_attempts);
        EXPECT_EQ(telemetry.trim.reclaimed_items, state.reclaimed_items);
        EXPECT_EQ(telemetry.trim.reclaimed_bytes, state.reclaimed_bytes);

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(config, bytes_a, bytes_b, &el));
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  TelemetryDisabledExpectedToSuppressCounterAccumulation)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 4096,
    .trim_mode = oxygen::content::ResidencyTrimMode::kManual,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xEE);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](AssetLoaderConfig config, std::vector<uint8_t> bytes,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        loader.SetTelemetryEnabled(false);

        const auto key = loader.MintSyntheticBufferKey();
        auto first = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> { .key = key, .bytes = bytes });
        EXPECT_THAT(first, NotNull());

        auto second = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> { .key = key, .bytes = bytes });
        EXPECT_THAT(second, NotNull());

        const auto telemetry = loader.GetTelemetryStats();
        EXPECT_FALSE(telemetry.telemetry_enabled);
        EXPECT_EQ(telemetry.buffer_resources.at(
                    AssetLoader::TypedLoadMetric::kRequests),
          0U);
        EXPECT_EQ(telemetry.buffer_resources.at(
                    AssetLoader::TypedLoadMetric::kCacheHits),
          0U);
        EXPECT_EQ(telemetry.buffer_resources.at(
                    AssetLoader::TypedLoadMetric::kCacheMisses),
          0U);
        EXPECT_EQ(telemetry.trim.manual_attempts, 0U);
        EXPECT_EQ(telemetry.trim.auto_attempts, 0U);
        EXPECT_GE(telemetry.cache.entries, 1U);

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(config, bytes, &el));
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  ResidencyManualModeStorePressureExpectedToAvoidAutoTrim)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  static constexpr uint8_t kFillA = 0x51;
  static constexpr uint8_t kFillB = 0x52;

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
  oxygen::co::Run(el,
    [](AssetLoaderConfig config, std::vector<uint8_t> bytes_a,
      std::vector<uint8_t> bytes_b, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
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
        res_a.reset();
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
    }(config, bytes_a, bytes_b, &el));
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  AutoTrimPolicySwitchRuntimeExpectedToGateBehavior)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;

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
  oxygen::co::Run(el,
    [](AssetLoaderConfig config, std::vector<uint8_t> bytes_a,
      std::vector<uint8_t> bytes_b, std::vector<uint8_t> bytes_c,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        const auto key_a = loader.MintSyntheticBufferKey();
        const auto key_b = loader.MintSyntheticBufferKey();
        const auto key_c = loader.MintSyntheticBufferKey();

        auto res_a = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_a,
            .bytes = bytes_a,
          });
        EXPECT_THAT(res_a, NotNull());
        res_a.reset();
        EXPECT_TRUE(loader.HasBuffer(key_a));

        auto res_b = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_b,
            .bytes = bytes_b,
          });
        EXPECT_THAT(res_b, NotNull());
        EXPECT_TRUE(loader.HasBuffer(key_a));
        EXPECT_FALSE(loader.HasBuffer(key_b));
        EXPECT_EQ(loader.QueryResidencyPolicyState().trim_attempts, 0U);

        loader.SetResidencyPolicy(oxygen::content::ResidencyPolicy {
          .cache_budget_bytes = 1,
          .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
          .default_priority_class
          = oxygen::content::LoadPriorityClass::kDefault,
        });

        auto res_c = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_c,
            .bytes = bytes_c,
          });
        EXPECT_THAT(res_c, NotNull());
        EXPECT_TRUE(loader.HasBuffer(key_c));
        EXPECT_FALSE(loader.HasBuffer(key_a));
        EXPECT_GE(loader.QueryResidencyPolicyState().trim_attempts, 1U);

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(config, bytes_a, bytes_b, bytes_c, &el));
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  AutoTrimTelemetryMonotonicExpectedAcrossPressureEvents)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  TestEventLoop el;
  oxygen::co::Run(el,
    [](std::string hexdump, AssetLoaderConfig config,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
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
          res.reset();

          const auto cur = loader.QueryResidencyPolicyState();
          EXPECT_GE(cur.trim_attempts, prev.trim_attempts);
          EXPECT_GE(cur.reclaimed_items, prev.reclaimed_items);
          EXPECT_GE(cur.reclaimed_bytes, prev.reclaimed_bytes);
          prev = cur;
        }

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(hexdump, config, &el));
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  AutoTrimDeterministicVictimSelectionExpectedAcrossRepeatedRuns)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;

  auto run_once = [&] -> std::pair<bool, bool> {
    AssetLoaderConfig config {};
    config.residency_policy = oxygen::content::ResidencyPolicy {
      .cache_budget_bytes = 1,
      .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
      .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
    };

    const auto bytes_a
      = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x31);
    const auto bytes_b
      = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x32);

    TestEventLoop el;
    bool a_alive = false;
    bool b_alive = false;
    oxygen::co::Run(el,
      [](AssetLoaderConfig config, std::vector<uint8_t> bytes_a,
        std::vector<uint8_t> bytes_b, TestEventLoop* loop, bool* a_alive_out,
        bool* b_alive_out) -> Co<> {
        auto& el = *loop;
        auto& a_alive = *a_alive_out;
        auto& b_alive = *b_alive_out;
        oxygen::co::ThreadPool pool(el, 2);
        config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
        AssetLoader loader(Tag::Get(), config);
        loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

        OXCO_WITH_NURSERY(n)
        {
          co_await n.Start(&AssetLoader::ActivateAsync, &loader);
          loader.Run();
          const auto key_a = loader.MintSyntheticBufferKey();
          const auto key_b = loader.MintSyntheticBufferKey();

          auto a = co_await loader.LoadResourceAsync<BufferResource>(
            CookedResourceData<BufferResource> {
              .key = key_a,
              .bytes = std::span<const uint8_t>(bytes_a.data(), bytes_a.size()),
            });
          EXPECT_THAT(a, NotNull());
          a.reset();

          auto b = co_await loader.LoadResourceAsync<BufferResource>(
            CookedResourceData<BufferResource> {
              .key = key_b,
              .bytes = std::span<const uint8_t>(bytes_b.data(), bytes_b.size()),
            });
          EXPECT_THAT(b, NotNull());

          a_alive = loader.HasBuffer(key_a);
          b_alive = loader.HasBuffer(key_b);

          loader.Stop();
          co_return oxygen::co::kJoin;
        };
      }(config, bytes_a, bytes_b, &el, &a_alive, &b_alive));

    return std::pair { a_alive, b_alive };
  };

  const auto [a1, b1] = run_once();
  const auto [a2, b2] = run_once();
  EXPECT_EQ(a1, a2);
  EXPECT_EQ(b1, b2);
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  StoreFailureRetryAfterForcedTrimExpectedToCacheDecodedResource)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes_a = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x81);
  auto bytes_b = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x82);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](AssetLoaderConfig config, std::vector<uint8_t> bytes_a,
      std::vector<uint8_t> bytes_b, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        const auto key_a = loader.MintSyntheticBufferKey();
        const auto key_b = loader.MintSyntheticBufferKey();

        auto first = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_a,
            .bytes = bytes_a,
          });
        EXPECT_THAT(first, NotNull());
        first.reset();
        EXPECT_TRUE(loader.HasBuffer(key_a));

        auto second = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_b,
            .bytes = bytes_b,
          });
        EXPECT_THAT(second, NotNull());
        EXPECT_TRUE(loader.HasBuffer(key_b));
        EXPECT_FALSE(loader.HasBuffer(key_a));

        const auto state = loader.QueryResidencyPolicyState();
        EXPECT_GE(state.trim_attempts, 1U);

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(config, bytes_a, bytes_b, &el));
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  ManualTrimTelemetryExpectedToUpdateWithoutAutoMode)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kManual,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0x91);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](AssetLoaderConfig config, std::vector<uint8_t> bytes,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        const auto key = loader.MintSyntheticBufferKey();
        auto res = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> { .key = key, .bytes = bytes });
        EXPECT_THAT(res, NotNull());
        res.reset();
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
    }(config, bytes, &el));
}

NOLINT_TEST_F(
  AssetLoaderAutoTrimAsyncTest, PinnedResourceExpectedToBlockAutoTrimEviction)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 1,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes_a = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xA1);
  auto bytes_b = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xA2);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](AssetLoaderConfig config, std::vector<uint8_t> bytes_a,
      std::vector<uint8_t> bytes_b, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        const auto key_a = loader.MintSyntheticBufferKey();
        const auto key_b = loader.MintSyntheticBufferKey();
        auto res_a = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_a,
            .bytes = bytes_a,
          });
        EXPECT_THAT(res_a, NotNull());
        auto residency_pin = loader.PinResource(key_a);
        EXPECT_TRUE(residency_pin);

        res_a.reset();
        EXPECT_TRUE(loader.HasBuffer(key_a));

        auto res_b = co_await loader.LoadResourceAsync<BufferResource>(
          CookedResourceData<BufferResource> {
            .key = key_b,
            .bytes = bytes_b,
          });
        EXPECT_THAT(res_b, NotNull());

        EXPECT_TRUE(loader.HasBuffer(key_a));
        EXPECT_FALSE(loader.HasBuffer(key_b));
        residency_pin.Reset();

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(config, bytes_a, bytes_b, &el));
}

NOLINT_TEST_F(AssetLoaderAutoTrimAsyncTest,
  ResourcePipelinePressureCallbackExpectedToTriggerOnlyOnStorePressure)
{
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;

  AssetLoaderConfig config {};
  config.residency_policy = oxygen::content::ResidencyPolicy {
    .cache_budget_bytes = 16,
    .trim_mode = oxygen::content::ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
  };

  auto bytes = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xC1);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](AssetLoaderConfig config, std::vector<uint8_t> bytes,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
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
    }(config, bytes, &el));
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

  AssetLoader loader(Tag::Get(), config);
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
  static constexpr std::size_t kDataOffset = 256;
  static constexpr std::size_t kSizeBytes = 192;
  auto bytes_a = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xD1);
  auto bytes_b = MakeBytesFromHexdump(hexdump, kDataOffset + kSizeBytes, 0xD2);

  auto run_case = [&](const oxygen::content::ResidencyTrimMode mode)
    -> std::tuple<bool, bool, uint64_t> {
    AssetLoaderConfig config {};
    config.residency_policy = oxygen::content::ResidencyPolicy {
      .cache_budget_bytes = 1,
      .trim_mode = mode,
      .default_priority_class = oxygen::content::LoadPriorityClass::kDefault,
    };

    TestEventLoop el;
    struct Outcome {
      bool first_resident { false };
      bool second_resident { false };
      uint64_t trims { 0U };
    } outcome;

    oxygen::co::Run(el,
      [](std::vector<uint8_t> bytes_a, std::vector<uint8_t> bytes_b,
        AssetLoaderConfig config, TestEventLoop* loop,
        Outcome* outcome) -> Co<> {
        auto& el = *loop;
        oxygen::co::ThreadPool pool(el, 2);
        config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
        AssetLoader loader(Tag::Get(), config);
        loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);

        OXCO_WITH_NURSERY(n)
        {
          co_await n.Start(&AssetLoader::ActivateAsync, &loader);
          loader.Run();
          const auto key_a = loader.MintSyntheticBufferKey();
          const auto key_b = loader.MintSyntheticBufferKey();

          auto a = co_await loader.LoadResourceAsync<BufferResource>(
            CookedResourceData<BufferResource> {
              .key = key_a,
              .bytes = bytes_a,
            });
          EXPECT_THAT(a, NotNull());
          a.reset();

          auto b = co_await loader.LoadResourceAsync<BufferResource>(
            CookedResourceData<BufferResource> {
              .key = key_b,
              .bytes = bytes_b,
            });
          EXPECT_THAT(b, NotNull());

          outcome->first_resident = loader.HasBuffer(key_a);
          outcome->second_resident = loader.HasBuffer(key_b);
          outcome->trims = loader.QueryResidencyPolicyState().trim_attempts;
          loader.Stop();
          co_return oxygen::co::kJoin;
        };
      }(bytes_a, bytes_b, config, &el, &outcome));

    return std::tuple { outcome.first_resident, outcome.second_resident,
      outcome.trims };
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
