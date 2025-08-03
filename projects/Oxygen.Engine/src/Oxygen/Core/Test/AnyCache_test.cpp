//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/AnyCache.h>

using oxygen::AnyCache;

namespace {

// -----------------------------------------------------------------------------
// Basic test cases
// -----------------------------------------------------------------------------

class AnyCacheBasicTest : public testing::Test {
protected:
  struct CachedNumber final : oxygen::Object {
    OXYGEN_TYPED(CachedNumber)
  public:
    explicit CachedNumber(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  struct CachedString final : oxygen::Object {
    OXYGEN_TYPED(CachedString)
  public:
    explicit CachedString(const std::string_view v)
      : value(v)
    {
    }
    std::string value {};
  };

  // Create a cache with a small budget
  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
};

NOLINT_TEST_F(AnyCacheBasicTest, Smoke)
{
  cache_.Store(1, std::make_shared<CachedNumber>(1));
  cache_.Store(2, std::make_shared<CachedString>("two"));

  EXPECT_EQ(cache_.Peek<CachedNumber>(1)->value, 1);
  EXPECT_EQ(cache_.Peek<CachedString>(2)->value, "two");

  {
    const auto num = cache_.CheckOut<CachedNumber>(1);
    EXPECT_EQ(num->value, 1);
    cache_.CheckIn(1);
  }
  EXPECT_TRUE(cache_.Remove(1));

  {
    const auto str = cache_.CheckOut<CachedString>(2);
    EXPECT_EQ(str->value, "two");
    cache_.CheckIn(2);
  }
  EXPECT_TRUE(cache_.Remove(2));

  EXPECT_EQ(cache_.Size(), 0);
}

NOLINT_TEST_F(AnyCacheBasicTest, Ranges)
{
  using CachedNumberPtr = std::shared_ptr<CachedNumber>;
  const auto type_id = CachedNumber::ClassTypeId();

  cache_.Store(1, std::make_shared<CachedNumber>(1));
  cache_.Store(2, std::make_shared<CachedNumber>(2));
  cache_.Store(3, std::make_shared<CachedNumber>(3));

  cache_.Store(100, std::make_shared<CachedString>("str_100"));
  cache_.Store(101, std::make_shared<CachedString>("str_101"));
  cache_.Store(102, std::make_shared<CachedString>("str_102"));

  // ReSharper disable once CppLocalVariableMayBeConst
  auto cached_items = cache_.Keys() | std::views::filter([&](const int key) {
    return cache_.GetTypeId(key) == type_id;
  }) | std::views::transform([&](const int key) {
    return cache_.Peek<CachedNumber>(key);
  }) | std::views::filter([](const CachedNumberPtr& ptr) {
    return static_cast<bool>(ptr);
  });

  // Now you can iterate:
  std::vector<int> int_values;
  for (const auto& item : cached_items) {
    int_values.push_back(item->value);
  }

  EXPECT_EQ(int_values.size(), 3);
  EXPECT_THAT(int_values, ::testing::UnorderedElementsAre(1, 2, 3));
}

// -----------------------------------------------------------------------------
// Constructor and error test cases
// -----------------------------------------------------------------------------

class AnyCacheErrorTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };
};

//! Test constructor behavior with zero budget throws exception
NOLINT_TEST_F(AnyCacheErrorTest, Constructor_ZeroBudget_Throws)
{
  // Arrange, Act, Assert
  EXPECT_THROW(
    (AnyCache<int, oxygen::RefCountedEviction<int>>(0)), std::invalid_argument);
}

//! Test constructor with valid budget succeeds
NOLINT_TEST_F(AnyCacheErrorTest, Constructor_ValidBudget_Succeeds)
{
  // Arrange, Act
  AnyCache<int, oxygen::RefCountedEviction<int>> cache(100);

  // Assert
  EXPECT_EQ(cache.Budget(), 100);
  EXPECT_EQ(cache.Consumed(), 0);
  EXPECT_EQ(cache.Size(), 0);
}

// -----------------------------------------------------------------------------
// Store method test cases
// -----------------------------------------------------------------------------

class AnyCacheStoreTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_ { 10 };
};

//! Test storing nullptr value
NOLINT_TEST_F(AnyCacheStoreTest, Store_NullValue_Succeeds)
{
  // Arrange, Act
  const bool result = cache_.Store(1, std::shared_ptr<TestObject> {});

  // Assert
  EXPECT_TRUE(result);
  EXPECT_EQ(cache_.Size(), 1);
  EXPECT_TRUE(cache_.Contains(1));
  EXPECT_EQ(cache_.GetTypeId(1), oxygen::kInvalidTypeId);
}

//! Test storing valid object succeeds
NOLINT_TEST_F(AnyCacheStoreTest, Store_ValidObject_Succeeds)
{
  // Arrange
  auto obj = std::make_shared<TestObject>(42);

  // Act
  const bool result = cache_.Store(1, obj);

  // Assert
  EXPECT_TRUE(result);
  EXPECT_EQ(cache_.Size(), 1);
  EXPECT_TRUE(cache_.Contains(1));
  EXPECT_EQ(cache_.GetTypeId(1), TestObject::ClassTypeId());
  EXPECT_EQ(cache_.Consumed(), 1);
}

//! Test storing object when budget would be exceeded
NOLINT_TEST_F(
  AnyCacheStoreTest, Store_BudgetExceeded_RejectsThenSucceedsAfterEviction)
{
  // Arrange - fill cache to budget
  for (int i = 0; i < 10; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i));
  }
  EXPECT_EQ(cache_.Size(), 10);
  EXPECT_EQ(cache_.Consumed(), 10);

  // Act - try to store beyond budget
  const bool result1 = cache_.Store(100, std::make_shared<TestObject>(100));

  // Assert - should be rejected
  EXPECT_FALSE(result1);
  EXPECT_EQ(cache_.Size(), 10);
  EXPECT_FALSE(cache_.Contains(100));

  // Act - check in some items to make room
  cache_.CheckIn(0);
  cache_.CheckIn(1);
  EXPECT_EQ(cache_.Size(), 8); // Items should be evicted

  // Act - now storing should succeed
  const bool result2 = cache_.Store(100, std::make_shared<TestObject>(100));

  // Assert
  EXPECT_TRUE(result2);
  EXPECT_TRUE(cache_.Contains(100));
}

//! Test storing with same key replaces existing value
NOLINT_TEST_F(AnyCacheStoreTest, Store_ExistingKey_ReplacesValue)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  EXPECT_EQ(cache_.Peek<TestObject>(1)->value, 42);

  // Act
  const bool result = cache_.Store(1, std::make_shared<TestObject>(99));

  // Assert
  EXPECT_TRUE(result);
  EXPECT_EQ(cache_.Size(), 1);
  EXPECT_EQ(cache_.Peek<TestObject>(1)->value, 99);
}

//! Test storing fails when existing item is checked out
NOLINT_TEST_F(AnyCacheStoreTest, Store_ExistingKeyCheckedOut_Fails)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  auto checked_out = cache_.CheckOut<TestObject>(1);

  // Act
  const bool result = cache_.Store(1, std::make_shared<TestObject>(99));

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(cache_.Peek<TestObject>(1)->value, 42); // Original value preserved

  // Cleanup
  cache_.CheckIn(1);
}

// -----------------------------------------------------------------------------
// Replace method test cases
// -----------------------------------------------------------------------------

class AnyCacheReplaceTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
};

//! Test replacing non-existent key fails
NOLINT_TEST_F(AnyCacheReplaceTest, Replace_NonExistentKey_Fails)
{
  // Arrange, Act
  const bool result = cache_.Replace(1, std::make_shared<TestObject>(42));

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(cache_.Size(), 0);
}

//! Test replacing existing key succeeds
NOLINT_TEST_F(AnyCacheReplaceTest, Replace_ExistingKey_Succeeds)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));

  // Act
  const bool result = cache_.Replace(1, std::make_shared<TestObject>(99));

  // Assert
  EXPECT_TRUE(result);
  EXPECT_EQ(cache_.Size(), 1);
  EXPECT_EQ(cache_.Peek<TestObject>(1)->value, 99);
}

//! Test replacing checked out item fails
NOLINT_TEST_F(AnyCacheReplaceTest, Replace_CheckedOutItem_Fails)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  auto checked_out = cache_.CheckOut<TestObject>(1);

  // Act
  const bool result = cache_.Replace(1, std::make_shared<TestObject>(99));

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(cache_.Peek<TestObject>(1)->value, 42); // Original preserved

  // Cleanup
  cache_.CheckIn(1);
}

// -----------------------------------------------------------------------------
// CheckOut/CheckIn test cases
// -----------------------------------------------------------------------------

class AnyCacheCheckOutTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  struct OtherObject final : oxygen::Object {
    OXYGEN_TYPED(OtherObject)
  public:
    explicit OtherObject(const std::string& v)
      : value(v)
    {
    }
    std::string value;
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
};

//! Test checking out non-existent key returns null
NOLINT_TEST_F(AnyCacheCheckOutTest, CheckOut_NonExistentKey_ReturnsNull)
{
  // Arrange, Act
  auto result = cache_.CheckOut<TestObject>(1);

  // Assert
  EXPECT_FALSE(result);
}

//! Test checking out existing item with correct type succeeds
NOLINT_TEST_F(AnyCacheCheckOutTest, CheckOut_ExistingItemCorrectType_Succeeds)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Initially checked out after Store

  // Act
  auto result = cache_.CheckOut<TestObject>(1);

  // Assert
  EXPECT_TRUE(result);
  EXPECT_EQ(result->value, 42);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 2)

  // Cleanup
  cache_.CheckIn(1);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 1)
  cache_.CheckIn(1);
  EXPECT_FALSE(cache_.IsCheckedOut(1)); // Back to not checked out (count = 0)
} //! Test checking out with wrong type returns null
NOLINT_TEST_F(AnyCacheCheckOutTest, CheckOut_WrongType_ReturnsNull)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));

  // Act
  auto result = cache_.CheckOut<OtherObject>(1);

  // Assert
  EXPECT_FALSE(result);
}

//! Test multiple checkouts increment reference count
NOLINT_TEST_F(
  AnyCacheCheckOutTest, CheckOut_MultipleCheckouts_IncrementsRefCount)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Initially checked out after Store
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1); // Initial checkout count

  // Act
  auto result1 = cache_.CheckOut<TestObject>(1);
  auto result2 = cache_.CheckOut<TestObject>(1);
  auto result3 = cache_.CheckOut<TestObject>(1);

  // Assert
  EXPECT_TRUE(result1);
  EXPECT_TRUE(result2);
  EXPECT_TRUE(result3);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Checked out (checkout count = 4)
  EXPECT_EQ(cache_.GetCheckoutCount(1), 4); // Verify exact count

  // Cleanup
  cache_.CheckIn(1);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 3)
  EXPECT_EQ(cache_.GetCheckoutCount(1), 3);
  cache_.CheckIn(1);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 2)
  EXPECT_EQ(cache_.GetCheckoutCount(1), 2);
  cache_.CheckIn(1);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 1)
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1);
  cache_.CheckIn(1);
  EXPECT_FALSE(cache_.IsCheckedOut(1)); // Now not checked out (count = 0)
  EXPECT_EQ(cache_.GetCheckoutCount(1), 0);
}

//! Test check in reduces reference count and evicts when zero
NOLINT_TEST_F(AnyCacheCheckOutTest, CheckIn_ReducesRefCountAndEvicts)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  auto checked_out = cache_.CheckOut<TestObject>(1);
  EXPECT_EQ(cache_.Size(), 1);

  // Act - first check in brings refcount from 2 to 1, item stays
  cache_.CheckIn(1);
  EXPECT_EQ(cache_.Size(), 1);
  EXPECT_TRUE(cache_.Contains(1));

  // Act - second check in brings refcount from 1 to 0, should evict
  cache_.CheckIn(1);

  // Assert
  EXPECT_EQ(cache_.Size(), 0);
  EXPECT_FALSE(cache_.Contains(1));
}

//! Test check in on non-existent key is safe
NOLINT_TEST_F(AnyCacheCheckOutTest, CheckIn_NonExistentKey_Safe)
{
  // Arrange, Act, Assert
  EXPECT_NO_THROW(cache_.CheckIn(999));
}

// -----------------------------------------------------------------------------
// Touch method test cases
// -----------------------------------------------------------------------------

class AnyCacheTouchTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
};

//! Test touch on non-existent key is safe
NOLINT_TEST_F(AnyCacheTouchTest, Touch_NonExistentKey_Safe)
{
  // Arrange, Act, Assert
  EXPECT_NO_THROW(cache_.Touch(999));
}

//! Test touch increments reference count without returning value
NOLINT_TEST_F(AnyCacheTouchTest, Touch_ExistingKey_IncrementsRefCount)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Initially checked out after Store

  // Act
  cache_.Touch(1);

  // Assert - Touch should increment checkout count
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 2)
  cache_.CheckIn(1);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 1)
  EXPECT_EQ(cache_.Size(), 1); // Still there, refcount back to 1
  EXPECT_TRUE(cache_.Contains(1));

  // Final check-in should evict
  cache_.CheckIn(1);
  EXPECT_EQ(cache_.Size(), 0);
}

// -----------------------------------------------------------------------------
// Peek method test cases
// -----------------------------------------------------------------------------

class AnyCachePeekTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  struct OtherObject final : oxygen::Object {
    OXYGEN_TYPED(OtherObject)
  public:
    explicit OtherObject(const std::string& v)
      : value(v)
    {
    }
    std::string value;
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
};

//! Test peek on non-existent key returns null
NOLINT_TEST_F(AnyCachePeekTest, Peek_NonExistentKey_ReturnsNull)
{
  // Arrange, Act
  auto result = cache_.Peek<TestObject>(999);

  // Assert
  EXPECT_FALSE(result);
}

//! Test peek with correct type returns value without affecting ref count
NOLINT_TEST_F(
  AnyCachePeekTest, Peek_CorrectType_ReturnsValueWithoutRefCountChange)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));

  // Act
  auto result = cache_.Peek<TestObject>(1);

  // Assert
  EXPECT_TRUE(result);
  EXPECT_EQ(result->value, 42);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Initially checked out after Store

  // Single check-in should evict since peek doesn't affect ref count
  cache_.CheckIn(1);
  EXPECT_EQ(cache_.Size(), 0);
}

//! Test peek with wrong type returns null
NOLINT_TEST_F(AnyCachePeekTest, Peek_WrongType_ReturnsNull)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));

  // Act
  auto result = cache_.Peek<OtherObject>(1);

  // Assert
  EXPECT_FALSE(result);
}

// -----------------------------------------------------------------------------
// Remove method test cases
// -----------------------------------------------------------------------------

class AnyCacheRemoveTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
};

//! Test remove on non-existent key returns false
NOLINT_TEST_F(AnyCacheRemoveTest, Remove_NonExistentKey_ReturnsFalse)
{
  // Arrange, Act
  const bool result = cache_.Remove(999);

  // Assert
  EXPECT_FALSE(result);
}

//! Test remove on checked out item fails
NOLINT_TEST_F(AnyCacheRemoveTest, Remove_CheckedOutItem_Fails)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  auto checked_out = cache_.CheckOut<TestObject>(1);

  // Act
  const bool result = cache_.Remove(1);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_TRUE(cache_.Contains(1));

  // Cleanup
  cache_.CheckIn(1);
}

//! Test remove on non-checked out item succeeds
NOLINT_TEST_F(AnyCacheRemoveTest, Remove_NonCheckedOutItem_Succeeds)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));

  // Act
  const bool result = cache_.Remove(1);

  // Assert
  EXPECT_TRUE(result);
  EXPECT_FALSE(cache_.Contains(1));
  EXPECT_EQ(cache_.Size(), 0);
}

// -----------------------------------------------------------------------------
// Clear method test cases
// -----------------------------------------------------------------------------

class AnyCacheClearTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
  std::vector<std::tuple<int, std::shared_ptr<void>, oxygen::TypeId>>
    evicted_items_;

  auto SetupEvictionCallback() -> void
  {
    eviction_scope_
      = std::make_unique<decltype(cache_)::EvictionNotificationScope>(
        cache_.OnEviction([this](const int key, std::shared_ptr<void> value,
                            oxygen::TypeId type_id) {
          evicted_items_.emplace_back(key, value, type_id);
        }));
  }

private:
  std::unique_ptr<decltype(cache_)::EvictionNotificationScope> eviction_scope_;
};

//! Test clear on empty cache
NOLINT_TEST_F(AnyCacheClearTest, Clear_EmptyCache_Safe)
{
  // Arrange, Act
  cache_.Clear();

  // Assert
  EXPECT_EQ(cache_.Size(), 0);
  EXPECT_EQ(cache_.Consumed(), 0);
}

//! Test clear removes all items and calls eviction callback
NOLINT_TEST_F(AnyCacheClearTest, Clear_WithItems_RemovesAllAndCallsCallback)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(10));
  cache_.Store(2, std::make_shared<TestObject>(20));
  cache_.Store(3, std::make_shared<TestObject>(30));
  EXPECT_EQ(cache_.Size(), 3);

  {
    auto scope
      = cache_.OnEviction([this](const int key, std::shared_ptr<void> value,
                            oxygen::TypeId type_id) {
          evicted_items_.emplace_back(key, value, type_id);
        });

    // Act
    cache_.Clear();

    // Assert
    EXPECT_EQ(cache_.Size(), 0);
    EXPECT_EQ(cache_.Consumed(), 0);
    EXPECT_EQ(evicted_items_.size(), 3);

    // Verify all keys were evicted
    std::vector<int> evicted_keys;
    for (const auto& [key, value, type_id] : evicted_items_) {
      evicted_keys.push_back(key);
    }
    EXPECT_EQ(evicted_keys.size(), 3);
    EXPECT_TRUE(std::find(evicted_keys.begin(), evicted_keys.end(), 1)
      != evicted_keys.end());
    EXPECT_TRUE(std::find(evicted_keys.begin(), evicted_keys.end(), 2)
      != evicted_keys.end());
    EXPECT_TRUE(std::find(evicted_keys.begin(), evicted_keys.end(), 3)
      != evicted_keys.end());
  }
}

// -----------------------------------------------------------------------------
// Utility method test cases
// -----------------------------------------------------------------------------

class AnyCacheUtilityTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_ { 10 };
};

//! Test Contains method
NOLINT_TEST_F(AnyCacheUtilityTest, Contains_ExistingAndNonExistingKeys)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));

  // Act & Assert
  EXPECT_TRUE(cache_.Contains(1));
  EXPECT_FALSE(cache_.Contains(999));
}

//! Test GetTypeId method
NOLINT_TEST_F(AnyCacheUtilityTest, GetTypeId_ExistingAndNonExistingKeys)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  cache_.Store(2, std::shared_ptr<TestObject> {}); // null value

  // Act & Assert
  EXPECT_EQ(cache_.GetTypeId(1), TestObject::ClassTypeId());
  EXPECT_EQ(cache_.GetTypeId(2), oxygen::kInvalidTypeId);
  EXPECT_EQ(cache_.GetTypeId(999), oxygen::kInvalidTypeId);
}

//! Test Size method
NOLINT_TEST_F(AnyCacheUtilityTest, Size_ReflectsActualCount)
{
  // Arrange & Assert initial state
  EXPECT_EQ(cache_.Size(), 0);

  // Act & Assert - add items
  cache_.Store(1, std::make_shared<TestObject>(1));
  EXPECT_EQ(cache_.Size(), 1);

  cache_.Store(2, std::make_shared<TestObject>(2));
  EXPECT_EQ(cache_.Size(), 2);

  // Act & Assert - remove item
  cache_.Remove(1);
  EXPECT_EQ(cache_.Size(), 1);

  // Act & Assert - clear all
  cache_.Clear();
  EXPECT_EQ(cache_.Size(), 0);
}

//! Test Budget and Consumed methods
NOLINT_TEST_F(AnyCacheUtilityTest, BudgetAndConsumed_TrackResourceUsage)
{
  // Arrange & Assert initial state
  EXPECT_EQ(cache_.Budget(), 10);
  EXPECT_EQ(cache_.Consumed(), 0);

  // Act & Assert - add items
  cache_.Store(1, std::make_shared<TestObject>(1));
  EXPECT_EQ(cache_.Consumed(), 1);

  cache_.Store(2, std::make_shared<TestObject>(2));
  EXPECT_EQ(cache_.Consumed(), 2);

  // Act & Assert - remove item
  cache_.Remove(1);
  EXPECT_EQ(cache_.Consumed(), 1);

  // Act & Assert - clear all
  cache_.Clear();
  EXPECT_EQ(cache_.Consumed(), 0);
}

//! Test IsCheckedOut method
NOLINT_TEST_F(AnyCacheUtilityTest, IsCheckedOut_ReflectsCheckoutState)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));

  // Assert - initially checked out after Store
  EXPECT_TRUE(cache_.IsCheckedOut(1));

  // Act & Assert - check out increases checkout count
  auto obj = cache_.CheckOut<TestObject>(1);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 2)

  // Act & Assert - check in once (count 2 -> 1)
  cache_.CheckIn(1);
  EXPECT_TRUE(cache_.IsCheckedOut(1)); // Still checked out (count = 1)

  // Act & Assert - final check in evicts (count 1 -> 0)
  cache_.CheckIn(1);
  EXPECT_FALSE(cache_.IsCheckedOut(999)); // Non-existent key
}

//! Test GetCheckoutCount method with various scenarios
NOLINT_TEST_F(AnyCacheUtilityTest, GetCheckoutCount_TracksCheckoutState)
{
  // Arrange & Assert - non-existent key returns 0
  EXPECT_EQ(cache_.GetCheckoutCount(999), 0);

  // Act & Assert - after Store, count should be 1
  cache_.Store(1, std::make_shared<TestObject>(42));
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1);

  // Act & Assert - after CheckOut, count should be 2
  auto obj1 = cache_.CheckOut<TestObject>(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 2);

  // Act & Assert - after another CheckOut, count should be 3
  auto obj2 = cache_.CheckOut<TestObject>(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 3);

  // Act & Assert - after Touch, count should be 4
  cache_.Touch(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 4);

  // Act & Assert - after CheckIn, count should be 3
  cache_.CheckIn(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 3);

  // Act & Assert - after another CheckIn, count should be 2
  cache_.CheckIn(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 2);

  // Act & Assert - after another CheckIn, count should be 1
  cache_.CheckIn(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1);

  // Act & Assert - after final CheckIn, item evicted, count should be 0
  cache_.CheckIn(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 0);
  EXPECT_FALSE(cache_.Contains(1)); // Item should be evicted
}

//! Test GetCheckoutCount with multiple items
NOLINT_TEST_F(AnyCacheUtilityTest, GetCheckoutCount_MultipleItems)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(10));
  cache_.Store(2, std::make_shared<TestObject>(20));
  cache_.Store(3, std::make_shared<TestObject>(30));

  // Assert initial state
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1);
  EXPECT_EQ(cache_.GetCheckoutCount(2), 1);
  EXPECT_EQ(cache_.GetCheckoutCount(3), 1);

  // Act - check out item 1 multiple times
  auto obj1a = cache_.CheckOut<TestObject>(1);
  auto obj1b = cache_.CheckOut<TestObject>(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 3);

  // Act - check out item 2 once
  auto obj2 = cache_.CheckOut<TestObject>(2);
  EXPECT_EQ(cache_.GetCheckoutCount(2), 2);

  // Act - touch item 3
  cache_.Touch(3);
  EXPECT_EQ(cache_.GetCheckoutCount(3), 2);

  // Assert other items unchanged
  EXPECT_EQ(cache_.GetCheckoutCount(1), 3);
  EXPECT_EQ(cache_.GetCheckoutCount(2), 2);

  // Cleanup
  cache_.CheckIn(1); // 3 -> 2
  cache_.CheckIn(1); // 2 -> 1
  cache_.CheckIn(2); // 2 -> 1
  cache_.CheckIn(3); // 2 -> 1

  EXPECT_EQ(cache_.GetCheckoutCount(1), 1);
  EXPECT_EQ(cache_.GetCheckoutCount(2), 1);
  EXPECT_EQ(cache_.GetCheckoutCount(3), 1);
}

// -----------------------------------------------------------------------------
// Eviction notification test cases
// -----------------------------------------------------------------------------

class AnyCacheEvictionTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
  std::vector<std::tuple<int, std::shared_ptr<void>, oxygen::TypeId>>
    evicted_items_;
};

//! Test eviction callback is called when items are removed
NOLINT_TEST_F(AnyCacheEvictionTest, EvictionCallback_CalledOnRemove)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  EXPECT_EQ(cache_.Size(), 1);
  EXPECT_TRUE(cache_.Contains(1));

  {
    auto scope
      = cache_.OnEviction([this](const int key, std::shared_ptr<void> value,
                            oxygen::TypeId type_id) {
          evicted_items_.emplace_back(key, value, type_id);
        });

    // Act
    bool removed = cache_.Remove(1);

    // Assert
    EXPECT_TRUE(removed); // Verify remove actually succeeded
    EXPECT_FALSE(cache_.Contains(1)); // Verify item was removed
    EXPECT_EQ(cache_.Size(), 0);
    EXPECT_EQ(evicted_items_.size(), 1);
    if (!evicted_items_.empty()) {
      EXPECT_EQ(std::get<0>(evicted_items_[0]), 1);
      EXPECT_EQ(std::get<2>(evicted_items_[0]), TestObject::ClassTypeId());
    }
  }
}

//! Test eviction callback is called when items are evicted on check-in
NOLINT_TEST_F(AnyCacheEvictionTest, EvictionCallback_CalledOnCheckInEviction)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));

  {
    auto scope
      = cache_.OnEviction([this](const int key, std::shared_ptr<void> value,
                            oxygen::TypeId type_id) {
          evicted_items_.emplace_back(key, value, type_id);
        });

    // Act - check in should evict
    cache_.CheckIn(1);

    // Assert
    EXPECT_EQ(evicted_items_.size(), 1);
    EXPECT_EQ(std::get<0>(evicted_items_[0]), 1);
  }
}

//! Test eviction scope is properly scoped
NOLINT_TEST_F(AnyCacheEvictionTest, EvictionScope_ProperlyScoped)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  cache_.Store(2, std::make_shared<TestObject>(99));

  // Act & Assert - callback active within scope
  {
    auto scope
      = cache_.OnEviction([this](const int key, std::shared_ptr<void> value,
                            oxygen::TypeId type_id) {
          evicted_items_.emplace_back(key, value, type_id);
        });

    cache_.Remove(1);
    EXPECT_EQ(evicted_items_.size(), 1);
  }

  // Act & Assert - callback inactive outside scope
  cache_.Remove(2);
  EXPECT_EQ(evicted_items_.size(), 1); // No change
}

// -----------------------------------------------------------------------------
// Edge case and complex scenario tests
// -----------------------------------------------------------------------------

class AnyCacheEdgeTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_ { 5 };
};

//! Test complex checkout/checkin scenario with budget constraints
NOLINT_TEST_F(AnyCacheEdgeTest, ComplexCheckoutScenario_BudgetConstraints)
{
  // Simple test first - verify CheckIn on non-checked-out item evicts it
  cache_.Store(99, std::make_shared<TestObject>(99));
  EXPECT_EQ(cache_.Size(), 1);
  cache_.CheckIn(99); // Should evict since refcount goes 1->0
  EXPECT_EQ(cache_.Size(), 0);

  // Now the real test
  // Arrange - fill cache to budget
  for (int i = 0; i < 5; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i));
  }
  EXPECT_EQ(cache_.Size(), 5);
  EXPECT_EQ(cache_.Consumed(), 5);

  // Act - check out some items, creating additional references
  auto obj0 = cache_.CheckOut<TestObject>(0);
  auto obj1 = cache_.CheckOut<TestObject>(1);
  auto obj2 = cache_.CheckOut<TestObject>(2);

  // Act - check in items 3 and 4 to evict them (refcount 1->0)
  cache_.CheckIn(3); // This should evict item 3
  cache_.CheckIn(4); // This should evict item 4
  EXPECT_EQ(cache_.Size(), 3); // Items 0, 1, 2 remain (they have refcount 2)

  // Act - now we should be able to store new items
  EXPECT_TRUE(cache_.Store(10, std::make_shared<TestObject>(10)));
  EXPECT_TRUE(cache_.Store(11, std::make_shared<TestObject>(11)));
  EXPECT_EQ(cache_.Size(), 5);

  // Act - check in remaining items to reduce refcount from 2 to 1
  cache_.CheckIn(0);
  cache_.CheckIn(1);
  cache_.CheckIn(2);
  EXPECT_EQ(cache_.Size(), 5); // All items still there (refcount = 1 each)

  // Now check them in again to fully evict (1 -> 0)
  cache_.CheckIn(0);
  cache_.CheckIn(1);
  cache_.CheckIn(2);
  EXPECT_EQ(cache_.Size(), 2); // Only 10 and 11 remain
}

//! Test that replace calls eviction callback for old value
NOLINT_TEST_F(AnyCacheEdgeTest, Replace_CallsEvictionCallbackForOldValue)
{
  // Arrange
  cache_.Store(1, std::make_shared<TestObject>(42));
  std::vector<std::tuple<int, std::shared_ptr<void>, oxygen::TypeId>>
    evicted_items;

  {
    auto scope = cache_.OnEviction(
      [&evicted_items](
        const int key, std::shared_ptr<void> value, oxygen::TypeId type_id) {
        evicted_items.emplace_back(key, value, type_id);
      });

    // Act
    EXPECT_TRUE(cache_.Replace(1, std::make_shared<TestObject>(99)));

    // Assert - old value should trigger eviction callback
    EXPECT_EQ(evicted_items.size(), 1);
    EXPECT_EQ(std::get<0>(evicted_items[0]), 1);
    EXPECT_EQ(
      cache_.Peek<TestObject>(1)->value, 99); // New value should be stored
  }
}

//! Test storing different types with same key after eviction
NOLINT_TEST_F(AnyCacheEdgeTest, Store_DifferentTypesAfterEviction)
{
  // Arrange
  struct StringObject final : oxygen::Object {
    OXYGEN_TYPED(StringObject)
  public:
    explicit StringObject(std::string v)
      : value(std::move(v))
    {
    }
    std::string value;
  };

  // Act & Assert - store number, then evict, then store string
  cache_.Store(1, std::make_shared<TestObject>(42));
  EXPECT_EQ(cache_.GetTypeId(1), TestObject::ClassTypeId());

  cache_.CheckIn(1); // Evict
  EXPECT_FALSE(cache_.Contains(1));

  cache_.Store(1, std::make_shared<StringObject>("hello"));
  EXPECT_EQ(cache_.GetTypeId(1), StringObject::ClassTypeId());
  EXPECT_EQ(cache_.Peek<StringObject>(1)->value, "hello");
}

} // namespace
