//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/CachePolicyContract.h>
#include <Oxygen/Core/LruEviction.h>
#include <Oxygen/Core/RefCountedEviction.h>
#include <Oxygen/Testing/GTest.h>

namespace {

// NOLINTBEGIN(*-magic-numbers)

using oxygen::AnyCache;
using oxygen::PolicyBudgetStatus;
using oxygen::TypeId;

struct MockValue final : public oxygen::Object {
  OXYGEN_TYPED(MockValue)
public:
  explicit MockValue(const size_t size_in_bytes)
    : size_in_bytes_(size_in_bytes)
  {
  }

  [[nodiscard]] auto ByteSize() const noexcept -> size_t
  {
    return size_in_bytes_;
  }

private:
  size_t size_in_bytes_ { 0 };
};

auto LruByteCost(const std::shared_ptr<void>& value, const TypeId /*unused*/)
  -> size_t
{
  return std::static_pointer_cast<const MockValue>(value)->ByteSize();
}

NOLINT_TEST(AnyCacheBudgetTest, PublicationOwnsUsageBeforeEvictionCallbacks)
{
  AnyCache<int, oxygen::LruEviction<int>> cache(1);
  ASSERT_TRUE(cache.Store(1, std::make_shared<MockValue>(1)));
  auto scope = cache.OnEviction([&](const auto& retired) {
    EXPECT_EQ(retired.entry.GetKey(), 1);
    EXPECT_EQ(cache.GetCheckoutCount(2), 2U);
    EXPECT_FALSE(cache.Remove(2));
    static_cast<void>(cache.SetBudget(3));
  });
  auto loaded = cache.StoreAndAcquire(
    2, std::make_shared<MockValue>(1), oxygen::CheckoutOwner::kExternal);
  if (!loaded) {
    FAIL() << "Publication must return the installed usage";
  }
  EXPECT_EQ(cache.Budget(), 3U);
  EXPECT_TRUE(cache.Release(std::move(loaded->ticket)));
}

NOLINT_TEST(
  AnyCacheBudgetTest, CallbackClearCannotChargeReplacementForOldResult)
{
  AnyCache<int, oxygen::LruEviction<int>> cache(1);
  ASSERT_TRUE(cache.Store(1, std::make_shared<MockValue>(1)));
  auto scope = cache.OnEviction([&](const auto& retired) {
    if (retired.entry.GetKey() == 1) {
      cache.Clear();
      ASSERT_TRUE(cache.Store(2, std::make_shared<MockValue>(2)));
    }
  });
  auto loaded = cache.StoreAndAcquire(
    2, std::make_shared<MockValue>(1), oxygen::CheckoutOwner::kExternal);
  if (!loaded) {
    FAIL() << "Accepted publication retains its original payload";
  }
  EXPECT_EQ(loaded->value->ByteSize(), 1U);
  EXPECT_FALSE(cache.Release(std::move(loaded->ticket)));
  EXPECT_EQ(cache.GetCheckoutCount(2), 1U);
  EXPECT_EQ(cache.Peek<MockValue>(2)->ByteSize(), 2U);
}

NOLINT_TEST(AnyCacheBudgetTest, ThrowingCallbackDoesNotLeakUndeliveredUsage)
{
  AnyCache<int, oxygen::LruEviction<int>> cache(1);
  ASSERT_TRUE(cache.Store(1, std::make_shared<MockValue>(1)));
  auto scope = cache.OnEviction(
    [](const auto&) { throw std::runtime_error("notification failed"); });
  EXPECT_THROW(
    static_cast<void>(cache.StoreAndAcquire(
      2, std::make_shared<MockValue>(1), oxygen::CheckoutOwner::kExternal)),
    std::runtime_error);
  EXPECT_EQ(cache.GetCheckoutCount(2), 1U);
}

NOLINT_TEST(AnyCacheBudgetTest, CallbackReplacementDoesNotDestroyActiveCallable)
{
  using Cache = AnyCache<int, oxygen::LruEviction<int>>;
  Cache cache(2);
  ASSERT_TRUE(cache.Store(1, std::make_shared<MockValue>(1)));
  ASSERT_TRUE(cache.Store(2, std::make_shared<MockValue>(1)));
  std::optional<Cache::EvictionNotificationScope> nested;
  int calls = 0;
  auto scope = cache.OnEviction([&](const auto&) {
    ++calls;
    if (!nested) {
      nested.emplace(cache.OnEviction([](const auto&) { }));
    }
  });
  cache.Clear();
  EXPECT_EQ(calls, 2);
  nested.reset();
}

struct FailingAdmissionPolicy : oxygen::LruEviction<int> {
  using LruEviction::LruEviction;
  std::optional<unsigned int> admissions_before_failure {};

  auto Store(EntryType&& entry) -> IteratorType
  {
    if (admissions_before_failure) {
      if (*admissions_before_failure == 0U) {
        throw std::bad_alloc {};
      }
      --*admissions_before_failure;
    }
    return LruEviction::Store(std::move(entry));
  }
};

NOLINT_TEST(AnyCacheBudgetTest, FailedRetryStillDeliversCommittedRetirements)
{
  AnyCache<int, FailingAdmissionPolicy> cache(1);
  ASSERT_TRUE(cache.Store(1, std::make_shared<MockValue>(1)));
  cache.GetPolicy().admissions_before_failure = 1U;
  int retired_count = 0;
  auto scope = cache.OnEviction([&](const auto& retired) {
    EXPECT_EQ(retired.entry.GetKey(), 1);
    EXPECT_EQ(cache.Size(), 0U);
    ++retired_count;
  });
  EXPECT_THROW(
    static_cast<void>(cache.StoreAndAcquire(
      2, std::make_shared<MockValue>(1), oxygen::CheckoutOwner::kExternal)),
    std::bad_alloc);
  EXPECT_EQ(retired_count, 1);
  EXPECT_EQ(cache.Budget(), 1U);
  EXPECT_EQ(cache.Consumed(), 0U);
  EXPECT_FALSE(cache.Contains(2));
}

NOLINT_TEST(AnyCacheBudgetTest, SetBudgetSafetyRegression)
{
  using Policy = oxygen::LruEviction<int>;
  AnyCache<int, Policy> cache(1000);
  cache.GetPolicy().SetCostFunction(LruByteCost);

  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(cache.Store(i, std::make_shared<MockValue>(100)));
  }
  ASSERT_EQ(cache.Consumed(), 1000);

  NOLINT_EXPECT_NO_THROW({ cache.SetBudget(250); });

  EXPECT_LE(cache.Consumed(), 250);
  EXPECT_EQ(cache.Budget(), 250);
}

NOLINT_TEST(AnyCacheBudgetTest, BudgetReductionTriggersEviction)
{
  using Policy = oxygen::LruEviction<int>;
  AnyCache<int, Policy> cache(100);
  cache.GetPolicy().SetCostFunction(LruByteCost);

  for (int i = 0; i < 10; ++i) {
    cache.Store(i, std::make_shared<MockValue>(10));
  }
  ASSERT_EQ(cache.Consumed(), 100);

  cache.SetBudget(30);
  EXPECT_EQ(cache.Consumed(), 30);
  EXPECT_FALSE(cache.Contains(0));
  EXPECT_FALSE(cache.Contains(6));
  EXPECT_TRUE(cache.Contains(7));
  EXPECT_TRUE(cache.Contains(9));
}

NOLINT_TEST(AnyCacheBudgetTest, SetBudgetReturnsEnforcedStatusForLruPolicy)
{
  using Policy = oxygen::LruEviction<int>;
  AnyCache<int, Policy> cache(100);
  cache.GetPolicy().SetCostFunction(LruByteCost);

  for (int i = 0; i < 10; ++i) {
    cache.Store(i, std::make_shared<MockValue>(10));
  }

  const auto status = cache.SetBudget(30);
  EXPECT_EQ(status, PolicyBudgetStatus::kSatisfied);
  EXPECT_LE(cache.Consumed(), cache.Budget());
}

NOLINT_TEST(AnyCacheBudgetTest, SetBudgetReturnsBestEffortForRefCountPolicy)
{
  AnyCache<int, oxygen::RefCountedEviction<int>> cache(8);
  cache.Store(1, std::make_shared<MockValue>(1));
  cache.Store(2, std::make_shared<MockValue>(1));
  cache.Store(3, std::make_shared<MockValue>(1));

  const auto status = cache.SetBudget(2);
  EXPECT_EQ(status, PolicyBudgetStatus::kUnsatisfiedBestEffort);
  EXPECT_TRUE(cache.IsOverBudget());
}

NOLINT_TEST(AnyCacheBudgetTest, SetBudgetEvictionTriggersEvictionCallback)
{
  using Policy = oxygen::LruEviction<int>;
  AnyCache<int, Policy> cache(100);
  cache.GetPolicy().SetCostFunction(LruByteCost);

  for (int i = 0; i < 10; ++i) {
    cache.Store(i, std::make_shared<MockValue>(10));
  }

  std::vector<int> evicted_keys;
  {
    auto scope = cache.OnEviction([&evicted_keys](const auto& retired) {
      const auto& key = retired.entry.GetKey();
      evicted_keys.push_back(key);
    });
    const auto status = cache.SetBudget(30);
    EXPECT_EQ(status, PolicyBudgetStatus::kSatisfied);
  }

  EXPECT_FALSE(evicted_keys.empty());
  EXPECT_EQ(cache.Size(), 3U);
}

NOLINT_TEST(AnyCacheBudgetTest, StoreAtExactBudgetEvictsToMakeRoomForLruPolicy)
{
  using Policy = oxygen::LruEviction<int>;
  AnyCache<int, Policy> cache(100);
  cache.GetPolicy().SetCostFunction(LruByteCost);

  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(cache.Store(i, std::make_shared<MockValue>(10)));
  }
  ASSERT_EQ(cache.Consumed(), 100);

  ASSERT_TRUE(cache.Store(10, std::make_shared<MockValue>(10)));
  EXPECT_EQ(cache.Consumed(), 100);
  EXPECT_EQ(cache.Size(), 10U);
  EXPECT_FALSE(cache.Contains(0));
  EXPECT_TRUE(cache.Contains(10));
}

NOLINT_TEST(AnyCacheBudgetTest, SetBudgetZeroThrows)
{
  AnyCache<int, oxygen::RefCountedEviction<int>> cache(8);
  EXPECT_THROW(cache.SetBudget(0), std::invalid_argument);
}

NOLINT_TEST(AnyCacheBudgetTest, SnapshotStatsTracksBudgetAndOverBudgetState)
{
  AnyCache<int, oxygen::RefCountedEviction<int>> cache(8);
  cache.Store(1, std::make_shared<MockValue>(1));
  cache.Store(2, std::make_shared<MockValue>(1));

  auto stats = cache.SnapshotStats();
  EXPECT_EQ(stats.size, 2U);
  EXPECT_EQ(stats.budget, 8U);
  EXPECT_EQ(stats.consumed, 2U);
  EXPECT_FALSE(stats.over_budget);

  cache.SetBudget(1);
  stats = cache.SnapshotStats();
  EXPECT_TRUE(stats.over_budget);
  EXPECT_TRUE(cache.IsOverBudget());
}

// NOLINTEND(*-magic-numbers)

} // namespace
