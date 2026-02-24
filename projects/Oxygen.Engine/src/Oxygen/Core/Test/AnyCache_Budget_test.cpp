//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <stdexcept>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/LruEviction.h>
#include <Oxygen/Core/RefCountedEviction.h>

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
    auto scope = cache.OnEviction(
      [&evicted_keys](const int key, const std::shared_ptr<void>&,
        const TypeId) { evicted_keys.push_back(key); });
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
