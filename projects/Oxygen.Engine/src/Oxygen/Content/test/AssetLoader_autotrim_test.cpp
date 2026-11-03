//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResidencyPin.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::co::Co;

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::CookedResourceData;
using oxygen::content::LoadPriorityClass;
using oxygen::content::ResidencyPin;
using oxygen::content::ResidencyPolicy;
using oxygen::content::ResidencyPolicyState;
using oxygen::content::ResidencyTrimMode;
using oxygen::content::ResourceKey;
using oxygen::content::testing::AssetLoaderBasicTest;
using oxygen::content::testing::AssetLoaderPoolTest;
using oxygen::content::testing::CookedVertexBuffer;
using oxygen::data::BufferResource;

namespace {

using TypedLoadMetric = AssetLoader::TypedLoadMetric;
using TelemetryStats = AssetLoader::TelemetryStats;

//! The payload of every buffer in this suite. The content cache costs a buffer
//! at its payload, so this is also what one cached buffer consumes: a budget of
//! `kBufferBytes` holds one buffer, `2 * kBufferBytes` holds two.
constexpr std::size_t kBufferBytes = 192;

//! A budget no suite buffer comes near, for the tests of the policy itself.
constexpr uint64_t kAmpleBudgetBytes = 4096;

auto MakePolicy(const uint64_t budget_bytes, const ResidencyTrimMode mode)
  -> ResidencyPolicy
{
  return ResidencyPolicy {
    .cache_budget_bytes = budget_bytes,
    .trim_mode = mode,
    .default_priority_class = LoadPriorityClass::kDefault,
  };
}

//! Loads the buffer `bytes` under `key`; null when the load fails.
auto LoadBuffer(AssetLoader* loader, const ResourceKey key,
  const std::span<const uint8_t> bytes) -> Co<std::shared_ptr<BufferResource>>
{
  co_return co_await loader->LoadResourceAsync<BufferResource>(
    CookedResourceData<BufferResource> { .key = key, .bytes = bytes });
}

//! What a `LoadPair` observed.
struct LoadedPair {
  ResourceKey first_key;
  ResourceKey second_key;
  std::shared_ptr<BufferResource> second;
  bool first_loaded;
  //! The first buffer was cached when the second was requested.
  bool first_cached;
  bool second_loaded;
  //! Residency once the second load completed.
  bool first_resident;
  bool second_resident;
  bool first_pinned;
  //! Trim passes run while the pair loaded.
  uint64_t trims;
  ResidencyPolicyState state;
  TelemetryStats telemetry;
};

//! Two buffers a test loads in turn, the first evicted for the second.
struct BufferPair {
  std::span<const uint8_t> first;
  std::span<const uint8_t> second;
};

//! Loads a buffer and releases it (it stays cached, unused), then loads a
//! second one and keeps it, reporting what the cache holds. The first one is
//! pinned while the second loads when `pin_first`.
auto LoadPair(AssetLoader* loader, const BufferPair buffers,
  const bool pin_first = false) -> Co<LoadedPair>
{
  const auto trims_before = loader->QueryResidencyPolicyState().trim_attempts;
  const auto first_key = loader->MintSyntheticBufferKey();
  const auto second_key = loader->MintSyntheticBufferKey();

  auto first = co_await LoadBuffer(loader, first_key, buffers.first);
  const auto first_loaded = first != nullptr;
  auto pin = pin_first ? loader->PinResource(first_key) : ResidencyPin {};
  first.reset();
  const auto first_cached = loader->HasBuffer(first_key);

  auto second = co_await LoadBuffer(loader, second_key, buffers.second);
  const auto state = loader->QueryResidencyPolicyState();
  co_return LoadedPair {
    .first_key = first_key,
    .second_key = second_key,
    .second = second,
    .first_loaded = first_loaded,
    .first_cached = first_cached,
    .second_loaded = second != nullptr,
    .first_resident = loader->HasBuffer(first_key),
    .second_resident = loader->HasBuffer(second_key),
    .first_pinned = static_cast<bool>(pin),
    .trims = state.trim_attempts - trims_before,
    .state = state,
    .telemetry = loader->GetTelemetryStats(),
  };
}

//! Runs a `LoadPair` per entry of `modes`, each under a policy of that trim
//! mode that holds one buffer, emptying the cache between them.
auto LoadPairsUnder(AssetLoader* loader,
  const std::array<ResidencyTrimMode, 2> modes, const BufferPair buffers)
  -> Co<std::array<LoadedPair, 2>>
{
  loader->SetResidencyPolicy(MakePolicy(kBufferBytes, modes.at(0)));
  auto first = co_await LoadPair(loader, buffers);
  first.second.reset();
  loader->TrimCache();

  loader->SetResidencyPolicy(MakePolicy(kBufferBytes, modes.at(1)));
  auto second = co_await LoadPair(loader, buffers);
  co_return std::array<LoadedPair, 2> { std::move(first), std::move(second) };
}

//! What `LoadPairThenSwitchToAuto` observed.
struct Switched {
  LoadedPair manual;
  ResidencyPolicyState after_switch;
  bool third_loaded;
  bool first_resident;
  bool third_resident;
  ResidencyPolicyState final_state;
};

//! Loads a pair under the loader's manual policy, switches it to automatic
//! trimming at the same budget, then loads a third buffer.
auto LoadPairThenSwitchToAuto(AssetLoader* loader, const BufferPair buffers,
  const std::span<const uint8_t> third_bytes) -> Co<Switched>
{
  auto manual = co_await LoadPair(loader, buffers);

  loader->SetResidencyPolicy(
    MakePolicy(kBufferBytes, ResidencyTrimMode::kAutoOnOverBudget));
  const auto after_switch = loader->QueryResidencyPolicyState();

  const auto third_key = loader->MintSyntheticBufferKey();
  const auto third = co_await LoadBuffer(loader, third_key, third_bytes);
  const auto first_resident = loader->HasBuffer(manual.first_key);
  co_return Switched {
    .manual = std::move(manual),
    .after_switch = after_switch,
    .third_loaded = third != nullptr,
    .first_resident = first_resident,
    .third_resident = loader->HasBuffer(third_key),
    .final_state = loader->QueryResidencyPolicyState(),
  };
}

//! What `LoadPairThenReloadSecond` observed.
struct Reloaded {
  LoadedPair pair;
  std::shared_ptr<BufferResource> again;
  TelemetryStats telemetry;
};

//! Loads a pair, then requests the second buffer again under its key.
auto LoadPairThenReloadSecond(AssetLoader* loader, const BufferPair buffers)
  -> Co<Reloaded>
{
  auto pair = co_await LoadPair(loader, buffers);
  auto again = co_await LoadBuffer(loader, pair.second_key, buffers.second);
  co_return Reloaded {
    .pair = std::move(pair),
    .again = std::move(again),
    .telemetry = loader->GetTelemetryStats(),
  };
}

//! Loads one buffer and keeps it, then requests it `repeats` more times.
struct Repeated {
  bool loaded;
  ResidencyPolicyState before_repeats;
  ResidencyPolicyState after_repeats;
  TelemetryStats telemetry;
};

auto LoadThenRepeat(AssetLoader* loader, const std::span<const uint8_t> bytes,
  const int repeats) -> Co<Repeated>
{
  const auto key = loader->MintSyntheticBufferKey();
  const auto first = co_await LoadBuffer(loader, key, bytes);
  const auto before = loader->QueryResidencyPolicyState();
  auto all_loaded = first != nullptr;
  for (auto i = 0; i < repeats; ++i) {
    const auto again = co_await LoadBuffer(loader, key, bytes);
    all_loaded = all_loaded && again == first;
  }
  co_return Repeated {
    .loaded = all_loaded,
    .before_repeats = before,
    .after_repeats = loader->QueryResidencyPolicyState(),
    .telemetry = loader->GetTelemetryStats(),
  };
}

//! Loads `count` distinct buffers one after the other, releasing each, and
//! reports the residency state after each load.
auto LoadSuccessively(AssetLoader* loader, const int count)
  -> Co<std::vector<ResidencyPolicyState>>
{
  std::vector<ResidencyPolicyState> states;
  for (auto i = 0; i < count; ++i) {
    const auto bytes = CookedVertexBuffer(
      kBufferBytes, std::byte { static_cast<uint8_t>(0x70 + i) });
    const auto key = loader->MintSyntheticBufferKey();
    auto buffer = co_await LoadBuffer(loader, key, bytes);
    EXPECT_NE(buffer, nullptr);
    buffer.reset();
    states.push_back(loader->QueryResidencyPolicyState());
  }
  co_return states;
}

//! What `LoadThenTrim` observed.
struct Trimmed {
  bool loaded;
  bool resident_before;
  bool resident_after;
  ResidencyPolicyState before;
  ResidencyPolicyState after;
};

//! Loads a buffer, releases it, and trims the cache by hand.
auto LoadThenTrim(AssetLoader* loader, const std::span<const uint8_t> bytes)
  -> Co<Trimmed>
{
  const auto key = loader->MintSyntheticBufferKey();
  auto buffer = co_await LoadBuffer(loader, key, bytes);
  const auto loaded = buffer != nullptr;
  buffer.reset();

  const auto resident_before = loader->HasBuffer(key);
  const auto before = loader->QueryResidencyPolicyState();
  loader->TrimCache();
  co_return Trimmed {
    .loaded = loaded,
    .resident_before = resident_before,
    .resident_after = loader->HasBuffer(key),
    .before = before,
    .after = loader->QueryResidencyPolicyState(),
  };
}

//! Loads the same buffer twice with telemetry disabled.
struct Untracked {
  bool loaded;
  TelemetryStats telemetry;
};

auto LoadTwiceWithoutTelemetry(
  AssetLoader* loader, const std::span<const uint8_t> bytes) -> Co<Untracked>
{
  loader->SetTelemetryEnabled(false);
  const auto key = loader->MintSyntheticBufferKey();
  const auto first = co_await LoadBuffer(loader, key, bytes);
  const auto second = co_await LoadBuffer(loader, key, bytes);
  co_return Untracked {
    .loaded = first != nullptr && second != nullptr,
    .telemetry = loader->GetTelemetryStats(),
  };
}

//! Residency of the suite's buffers, under policies of one-buffer budgets.
class AssetLoaderResidencyTest : public AssetLoaderPoolTest {
protected:
  auto UsePolicy(const uint64_t budget_bytes, const ResidencyTrimMode mode)
    -> void
  {
    loader_.SetResidencyPolicy(MakePolicy(budget_bytes, mode));
  }

  [[nodiscard]] auto A() const -> std::span<const uint8_t> { return a_; }
  [[nodiscard]] auto C() const -> std::span<const uint8_t> { return c_; }
  [[nodiscard]] auto Pair() const -> BufferPair
  {
    return { .first = a_, .second = b_ };
  }

private:
  std::vector<uint8_t> a_
    = CookedVertexBuffer(kBufferBytes, std::byte { 0x41 });
  std::vector<uint8_t> b_
    = CookedVertexBuffer(kBufferBytes, std::byte { 0x42 });
  std::vector<uint8_t> c_
    = CookedVertexBuffer(kBufferBytes, std::byte { 0x43 });
};

//=== Policy, without loading ===---------------------------------------------//

//! A policy given at construction is what the loader reports, as the policy and
//! as the state of an empty cache.
NOLINT_TEST_F(
  AssetLoaderBasicTest, ResidencyPolicyRoundTripExpectedToExposeState)
{
  AssetLoaderConfig config {};
  config.residency_policy = ResidencyPolicy {
    .cache_budget_bytes = kAmpleBudgetBytes,
    .trim_mode = ResidencyTrimMode::kAutoOnOverBudget,
    .default_priority_class = LoadPriorityClass::kCritical,
  };

  AssetLoader loader(Tag::Get(), config);

  const auto configured = loader.GetResidencyPolicy();
  EXPECT_EQ(configured.cache_budget_bytes, kAmpleBudgetBytes);
  EXPECT_EQ(configured.trim_mode, ResidencyTrimMode::kAutoOnOverBudget);
  EXPECT_EQ(configured.default_priority_class, LoadPriorityClass::kCritical);

  const auto queried = loader.QueryResidencyPolicyState();
  EXPECT_EQ(queried.policy.cache_budget_bytes, kAmpleBudgetBytes);
  EXPECT_EQ(queried.policy.trim_mode, ResidencyTrimMode::kAutoOnOverBudget);
  EXPECT_EQ(
    queried.policy.default_priority_class, LoadPriorityClass::kCritical);
  EXPECT_EQ(queried.cache_entries, 0U);
  EXPECT_EQ(queried.consumed_bytes, 0U);
  EXPECT_EQ(queried.checked_out_items, 0U);
  EXPECT_FALSE(queried.over_budget);
  EXPECT_EQ(queried.trim_attempts, 0U);
  EXPECT_EQ(queried.reclaimed_items, 0U);
  EXPECT_EQ(queried.reclaimed_bytes, 0U);
  EXPECT_EQ(queried.blocked_roots, 0U);
}

//! A policy with no budget is refused, and the loader keeps the policy it had.
NOLINT_TEST_F(AssetLoaderBasicTest, ResidencyPolicyZeroBudgetExpectedToThrow)
{
  AssetLoader loader(Tag::Get(), AssetLoaderConfig {});

  const auto original = loader.GetResidencyPolicy();
  EXPECT_GT(original.cache_budget_bytes, 0U);

  EXPECT_THROW(
    loader.SetResidencyPolicy(MakePolicy(0, ResidencyTrimMode::kManual)),
    std::invalid_argument);

  const auto after = loader.GetResidencyPolicy();
  EXPECT_EQ(after.cache_budget_bytes, original.cache_budget_bytes);
  EXPECT_EQ(after.trim_mode, original.trim_mode);
  EXPECT_EQ(after.default_priority_class, original.default_priority_class);
}

//! Querying the state twice with nothing in between reports the same policy
//! and the same trim counters.
NOLINT_TEST_F(AssetLoaderBasicTest,
  PolicyRoundTripIncludesTelemetryExpectedStableAfterNoopOperations)
{
  AssetLoaderConfig config {};
  config.residency_policy
    = MakePolicy(kAmpleBudgetBytes, ResidencyTrimMode::kAutoOnOverBudget);

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

//=== Automatic trimming ===--------------------------------------------------//

//! With automatic trimming and a budget of one buffer, loading a second buffer
//! evicts the unused first one and caches the second. The trim reclaims
//! exactly that buffer's bytes, and the loader's telemetry agrees with the
//! residency state.
NOLINT_TEST_F(AssetLoaderResidencyTest,
  ResidencyAutoTrimOnOverBudgetExpectedToReclaimAndRecordTelemetry)
{
  UsePolicy(kBufferBytes, ResidencyTrimMode::kAutoOnOverBudget);

  const auto pair_run = WhileActive(LoadPair(&loader_, Pair()));
  ASSERT_HAS_VALUE(pair_run);
  const auto& pair = *pair_run;

  EXPECT_NE(pair.first_key, pair.second_key);
  EXPECT_TRUE(pair.first_loaded);
  EXPECT_TRUE(pair.first_cached);
  EXPECT_TRUE(pair.second_loaded);
  EXPECT_FALSE(pair.first_resident);
  EXPECT_TRUE(pair.second_resident);

  const auto& state = pair.state;
  EXPECT_GE(state.trim_attempts, 1U);
  EXPECT_EQ(state.reclaimed_items, 1U);
  EXPECT_EQ(state.reclaimed_bytes, kBufferBytes);
  EXPECT_EQ(state.cache_entries, 1U);
  EXPECT_EQ(state.consumed_bytes, kBufferBytes);
  EXPECT_FALSE(state.over_budget);

  const auto& telemetry = pair.telemetry;
  EXPECT_EQ(telemetry.buffer_resources.at(TypedLoadMetric::kRequests), 2U);
  EXPECT_EQ(telemetry.buffer_resources.at(TypedLoadMetric::kCacheMisses), 2U);
  EXPECT_EQ(telemetry.buffer_resources.at(TypedLoadMetric::kTasksSpawned), 2U);
  EXPECT_EQ(telemetry.trim.manual_attempts + telemetry.trim.auto_attempts,
    state.trim_attempts);
  EXPECT_EQ(telemetry.trim.reclaimed_items, state.reclaimed_items);
  EXPECT_EQ(telemetry.trim.reclaimed_bytes, state.reclaimed_bytes);
}

//! The buffer a trim made room for is cached for good: requesting it again is
//! a cache hit on the same decoded buffer, not a second decode.
NOLINT_TEST_F(AssetLoaderResidencyTest,
  StoreFailureRetryAfterForcedTrimExpectedToCacheDecodedResource)
{
  UsePolicy(kBufferBytes, ResidencyTrimMode::kAutoOnOverBudget);

  const auto reloaded_run
    = WhileActive(LoadPairThenReloadSecond(&loader_, Pair()));
  ASSERT_HAS_VALUE(reloaded_run);
  const auto& reloaded = *reloaded_run;

  EXPECT_TRUE(reloaded.pair.second_resident);
  EXPECT_FALSE(reloaded.pair.first_resident);
  EXPECT_GE(reloaded.pair.state.trim_attempts, 1U);
  EXPECT_EQ(reloaded.again, reloaded.pair.second);
  EXPECT_EQ(
    reloaded.telemetry.buffer_resources.at(TypedLoadMetric::kCacheHits), 1U);
  EXPECT_EQ(
    reloaded.telemetry.buffer_resources.at(TypedLoadMetric::kTasksSpawned), 2U);
}

//! A pinned buffer is not evicted to make room: the trim reclaims nothing, the
//! pinned buffer stays cached, and the buffer that did not fit is still handed
//! to its requester.
NOLINT_TEST_F(
  AssetLoaderResidencyTest, PinnedResourceExpectedToBlockAutoTrimEviction)
{
  UsePolicy(kBufferBytes, ResidencyTrimMode::kAutoOnOverBudget);

  const auto pair_run = WhileActive(LoadPair(&loader_, Pair(), true));
  ASSERT_HAS_VALUE(pair_run);
  const auto& pair = *pair_run;

  EXPECT_TRUE(pair.first_pinned);
  EXPECT_TRUE(pair.first_cached);
  EXPECT_TRUE(pair.second_loaded);
  EXPECT_TRUE(pair.first_resident);
  EXPECT_FALSE(pair.second_resident);
  EXPECT_EQ(pair.state.reclaimed_items, 0U);
  EXPECT_EQ(pair.state.reclaimed_bytes, 0U);
  EXPECT_EQ(pair.state.consumed_bytes, kBufferBytes);
}

//! Automatic trimming runs only on store pressure: requests served from the
//! cache neither trim nor reclaim, whatever the mode asks for.
NOLINT_TEST_F(AssetLoaderResidencyTest,
  ResourcePipelinePressureCallbackExpectedToTriggerOnlyOnStorePressure)
{
  constexpr auto kRepeats = 5;
  UsePolicy(2 * kBufferBytes, ResidencyTrimMode::kAutoOnOverBudget);

  const auto repeated_run
    = WhileActive(LoadThenRepeat(&loader_, A(), kRepeats));
  ASSERT_HAS_VALUE(repeated_run);
  const auto& repeated = *repeated_run;

  EXPECT_TRUE(repeated.loaded);
  EXPECT_EQ(repeated.telemetry.buffer_resources.at(TypedLoadMetric::kCacheHits),
    static_cast<uint64_t>(kRepeats));
  EXPECT_EQ(repeated.after_repeats.trim_attempts, 0U);
  EXPECT_EQ(repeated.after_repeats.trim_attempts,
    repeated.before_repeats.trim_attempts);
  EXPECT_EQ(repeated.after_repeats.reclaimed_items,
    repeated.before_repeats.reclaimed_items);
  EXPECT_EQ(repeated.after_repeats.reclaimed_bytes,
    repeated.before_repeats.reclaimed_bytes);
  EXPECT_EQ(repeated.after_repeats.consumed_bytes, kBufferBytes);
}

//! Across successive pressure events the trim counters only grow, and each
//! event beyond the first evicts exactly the one buffer that no longer fits.
NOLINT_TEST_F(AssetLoaderResidencyTest,
  AutoTrimTelemetryMonotonicExpectedAcrossPressureEvents)
{
  constexpr auto kLoads = 3;
  UsePolicy(kBufferBytes, ResidencyTrimMode::kAutoOnOverBudget);

  const auto states_run = WhileActive(LoadSuccessively(&loader_, kLoads));
  ASSERT_HAS_VALUE(states_run);
  const auto& states = *states_run;

  ASSERT_EQ(states.size(), static_cast<std::size_t>(kLoads));
  ResidencyPolicyState previous {};
  for (std::size_t i = 0; i < states.size(); ++i) {
    const auto& current = states.at(i);
    EXPECT_GE(current.trim_attempts, previous.trim_attempts);
    EXPECT_GE(current.reclaimed_items, previous.reclaimed_items);
    EXPECT_GE(current.reclaimed_bytes, previous.reclaimed_bytes);
    EXPECT_EQ(current.reclaimed_items, i);
    EXPECT_EQ(current.reclaimed_bytes, i * kBufferBytes);
    EXPECT_EQ(current.consumed_bytes, kBufferBytes);
    previous = current;
  }
}

//! Which buffer pressure evicts does not vary: the same loads under the same
//! policy leave the same buffer resident every time.
NOLINT_TEST_F(AssetLoaderResidencyTest,
  AutoTrimDeterministicVictimSelectionExpectedAcrossRepeatedRuns)
{
  const auto runs_run = WhileActive(LoadPairsUnder(&loader_,
    {
      ResidencyTrimMode::kAutoOnOverBudget,
      ResidencyTrimMode::kAutoOnOverBudget,
    },
    Pair()));
  ASSERT_HAS_VALUE(runs_run);
  const auto& runs = *runs_run;

  const auto& first = runs.at(0);
  const auto& second = runs.at(1);
  EXPECT_FALSE(first.first_resident);
  EXPECT_TRUE(first.second_resident);
  EXPECT_EQ(first.first_resident, second.first_resident);
  EXPECT_EQ(first.second_resident, second.second_resident);
  EXPECT_EQ(first.state.consumed_bytes, second.state.consumed_bytes);
}

//=== Manual trimming ===-----------------------------------------------------//

//! With manual trimming, store pressure never trims: the buffer that does not
//! fit is handed to its requester uncached, and the cached one stays.
NOLINT_TEST_F(AssetLoaderResidencyTest,
  ResidencyManualModeStorePressureExpectedToAvoidAutoTrim)
{
  UsePolicy(kBufferBytes, ResidencyTrimMode::kManual);

  const auto pair_run = WhileActive(LoadPair(&loader_, Pair()));
  ASSERT_HAS_VALUE(pair_run);
  const auto& pair = *pair_run;

  EXPECT_TRUE(pair.first_loaded);
  EXPECT_TRUE(pair.first_cached);
  EXPECT_TRUE(pair.second_loaded);
  EXPECT_TRUE(pair.first_resident);
  EXPECT_FALSE(pair.second_resident);
  EXPECT_EQ(pair.state.policy.trim_mode, ResidencyTrimMode::kManual);
  EXPECT_EQ(pair.state.trim_attempts, 0U);
  EXPECT_EQ(pair.state.reclaimed_items, 0U);
  EXPECT_EQ(pair.state.reclaimed_bytes, 0U);
  EXPECT_EQ(pair.state.consumed_bytes, kBufferBytes);
}

//! A manual trim empties the cache of unused buffers and counts as one trim
//! pass reclaiming exactly the buffer's bytes, with automatic trimming off.
NOLINT_TEST_F(
  AssetLoaderResidencyTest, ManualTrimTelemetryExpectedToUpdateWithoutAutoMode)
{
  UsePolicy(kBufferBytes, ResidencyTrimMode::kManual);

  const auto trimmed_run = WhileActive(LoadThenTrim(&loader_, A()));
  ASSERT_HAS_VALUE(trimmed_run);
  const auto& trimmed = *trimmed_run;

  EXPECT_TRUE(trimmed.loaded);
  EXPECT_TRUE(trimmed.resident_before);
  EXPECT_EQ(trimmed.before.policy.trim_mode, ResidencyTrimMode::kManual);
  EXPECT_EQ(trimmed.before.consumed_bytes, kBufferBytes);
  EXPECT_EQ(trimmed.before.trim_attempts, 0U);

  EXPECT_FALSE(trimmed.resident_after);
  EXPECT_EQ(trimmed.after.trim_attempts, trimmed.before.trim_attempts + 1U);
  EXPECT_EQ(trimmed.after.reclaimed_items, 1U);
  EXPECT_EQ(trimmed.after.reclaimed_bytes, kBufferBytes);
  EXPECT_EQ(trimmed.after.consumed_bytes, 0U);
  EXPECT_EQ(trimmed.after.cache_entries, 0U);
}

//=== Switching the policy ===------------------------------------------------//

//! Changing the trim mode of a running loader takes effect on the next
//! pressure: under manual trimming the second buffer is refused, and after
//! switching to automatic trimming the third evicts the first.
NOLINT_TEST_F(
  AssetLoaderResidencyTest, AutoTrimPolicySwitchRuntimeExpectedToGateBehavior)
{
  UsePolicy(kBufferBytes, ResidencyTrimMode::kManual);

  const auto switched_run
    = WhileActive(LoadPairThenSwitchToAuto(&loader_, Pair(), C()));
  ASSERT_HAS_VALUE(switched_run);
  const auto& switched = *switched_run;

  EXPECT_TRUE(switched.manual.first_resident);
  EXPECT_FALSE(switched.manual.second_resident);
  EXPECT_EQ(switched.manual.state.trim_attempts, 0U);

  // The switch alone trims nothing: the cache is within its budget.
  EXPECT_EQ(switched.after_switch.policy.trim_mode,
    ResidencyTrimMode::kAutoOnOverBudget);
  EXPECT_EQ(switched.after_switch.trim_attempts, 0U);

  EXPECT_TRUE(switched.third_loaded);
  EXPECT_TRUE(switched.third_resident);
  EXPECT_FALSE(switched.first_resident);
  EXPECT_GE(switched.final_state.trim_attempts, 1U);
  EXPECT_EQ(switched.final_state.reclaimed_items, 1U);
  EXPECT_EQ(switched.final_state.reclaimed_bytes, kBufferBytes);
  EXPECT_EQ(switched.final_state.consumed_bytes, kBufferBytes);
}

//! The same two loads under a budget of one buffer end differently by trim
//! mode: manual keeps the first buffer and never trims, automatic evicts it
//! for the second.
NOLINT_TEST_F(AssetLoaderResidencyTest,
  CookedResourceManualVsAutoPressureExpectedDifferentResidencyOutcomes)
{
  const auto runs_run = WhileActive(LoadPairsUnder(&loader_,
    { ResidencyTrimMode::kManual, ResidencyTrimMode::kAutoOnOverBudget },
    Pair()));
  ASSERT_HAS_VALUE(runs_run);
  const auto& runs = *runs_run;

  const auto& manual = runs.at(0);
  EXPECT_TRUE(manual.first_resident);
  EXPECT_FALSE(manual.second_resident);
  EXPECT_EQ(manual.trims, 0U);

  const auto& automatic = runs.at(1);
  EXPECT_FALSE(automatic.first_resident);
  EXPECT_TRUE(automatic.second_resident);
  EXPECT_GE(automatic.trims, 1U);
}

//=== Telemetry ===-----------------------------------------------------------//

//! With telemetry disabled the load and trim counters stay at zero while the
//! cache still caches.
NOLINT_TEST_F(AssetLoaderResidencyTest,
  TelemetryDisabledExpectedToSuppressCounterAccumulation)
{
  UsePolicy(kBufferBytes, ResidencyTrimMode::kManual);

  const auto untracked_run
    = WhileActive(LoadTwiceWithoutTelemetry(&loader_, A()));
  ASSERT_HAS_VALUE(untracked_run);
  const auto& untracked = *untracked_run;

  EXPECT_TRUE(untracked.loaded);
  const auto& telemetry = untracked.telemetry;
  EXPECT_FALSE(telemetry.telemetry_enabled);
  EXPECT_EQ(telemetry.buffer_resources.at(TypedLoadMetric::kRequests), 0U);
  EXPECT_EQ(telemetry.buffer_resources.at(TypedLoadMetric::kCacheHits), 0U);
  EXPECT_EQ(telemetry.buffer_resources.at(TypedLoadMetric::kCacheMisses), 0U);
  EXPECT_EQ(telemetry.trim.manual_attempts, 0U);
  EXPECT_EQ(telemetry.trim.auto_attempts, 0U);
  EXPECT_EQ(telemetry.cache.entries, 1U);
}

} // namespace
