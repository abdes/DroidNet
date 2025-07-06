//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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

} // namespace
