//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Engine/Scripting/Detail/LruEviction.h>

using namespace oxygen;

namespace {
struct MockValue final : public oxygen::Object {
  OXYGEN_TYPED(MockValue)
public:
  size_t size_in_bytes { 0 };
  explicit MockValue(size_t s)
    : size_in_bytes(s)
  {
  }
};
}

NOLINT_TEST(AnyCacheBudgetTest, SetBudgetSafetyRegression)
{
  // Setup AnyCache with the LRU policy used by Scripting
  using Policy = scripting::detail::LruEviction<int>;
  AnyCache<int, Policy> cache(1000);

  cache.GetPolicy().cost_func
    = [](const std::shared_ptr<void>& value, const TypeId) -> size_t {
    return static_cast<const MockValue*>(value.get())->size_in_bytes;
  };

  // 1. Fill cache with items.
  // Store() marks them as checked out once (refcount 1).
  // In this state, they are eligible for eviction by Fit().
  for (int i = 0; i < 10; ++i) {
    auto val = std::make_shared<MockValue>(100);
    ASSERT_TRUE(cache.Store(i, val));
  }

  ASSERT_EQ(cache.Consumed(), 1000);

  // 2. Reduce budget significantly.
  // This triggers the Fit() loop which was previously crashing due to
  // re-entrancy if Remove() was called inside the loop. SetBudget handles this
  // safely.
  NOLINT_EXPECT_NO_THROW({ cache.SetBudget(250); });

  // 3. Verify state consistency
  EXPECT_LE(cache.Consumed(), 250);
  EXPECT_EQ(cache.Budget(), 250);
}

NOLINT_TEST(AnyCacheBudgetTest, BudgetReductionTriggersEviction)
{
  using Policy = scripting::detail::LruEviction<int>;
  AnyCache<int, Policy> cache(100);

  cache.GetPolicy().cost_func
    = [](const std::shared_ptr<void>& value, const TypeId) -> size_t {
    return static_cast<const MockValue*>(value.get())->size_in_bytes;
  };

  // Each item costs 10
  for (int i = 0; i < 10; ++i) {
    cache.Store(i, std::make_shared<MockValue>(10));
  }
  ASSERT_EQ(cache.Consumed(), 100);

  // Reduce budget to 30 (should keep 3 items)
  cache.SetBudget(30);

  EXPECT_EQ(cache.Consumed(), 30);

  // Verify that the MOST RECENT items are the ones kept (LRU behavior)
  // 0-6 should be gone, 7, 8, 9 should be there
  EXPECT_FALSE(cache.Contains(0));
  EXPECT_FALSE(cache.Contains(6));
  EXPECT_TRUE(cache.Contains(7));
  EXPECT_TRUE(cache.Contains(9));
}
