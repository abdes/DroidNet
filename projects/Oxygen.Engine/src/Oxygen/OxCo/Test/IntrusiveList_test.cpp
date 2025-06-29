//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Detail/IntrusiveList.h>

#include <ranges>

#include <Oxygen/Testing/GTest.h>

using namespace oxygen::co::detail;

class TestItem final : public IntrusiveListItem<TestItem> {
public:
  explicit TestItem(const int value)
    : value_(value)
  {
  }
  [[nodiscard]] auto GetValue() const -> int { return value_; }

private:
  int value_;
};

static_assert(std::ranges::forward_range<IntrusiveList<TestItem>>);

// Keep existing test
NOLINT_TEST(IntrusiveListTest, PushBack)
{
  IntrusiveList<TestItem> list;
  TestItem item1(1);
  TestItem item2(2);

  list.PushBack(item1);

  EXPECT_FALSE(list.Empty());
  EXPECT_EQ(list.Front().GetValue(), 1);
  EXPECT_EQ(list.Back().GetValue(), 1);

  list.PushBack(item2);

  EXPECT_FALSE(list.Empty());
  EXPECT_EQ(list.Front().GetValue(), 1);
  EXPECT_EQ(list.Back().GetValue(), 2);
}

// Add new tests
NOLINT_TEST(IntrusiveListTest, EmptyList)
{
  IntrusiveList<TestItem> list;
  EXPECT_TRUE(list.Empty());
  auto front = [&list] {
    const auto& f = list.Front();
    (void)f;
  };
  // NOLINTNEXTLINE
  EXPECT_THROW(front(), std::out_of_range);
  auto back = [&list] {
    const auto& b = list.Back();
    (void)b;
  };
  // NOLINTNEXTLINE
  EXPECT_THROW(back(), std::out_of_range);
}

NOLINT_TEST(IntrusiveListTest, PushFront)
{
  IntrusiveList<TestItem> list;
  TestItem item1(1);
  TestItem item2(2);

  list.PushFront(item1);

  EXPECT_FALSE(list.Empty());
  EXPECT_EQ(list.Front().GetValue(), 1);
  EXPECT_EQ(list.Back().GetValue(), 1);

  list.PushFront(item2);

  EXPECT_EQ(list.Front().GetValue(), 2);
  EXPECT_EQ(list.Back().GetValue(), 1);
}

NOLINT_TEST(IntrusiveListTest, PopOperations)
{
  IntrusiveList<TestItem> list;
  TestItem item1(1);
  TestItem item2(2);
  TestItem item3(3);

  list.PushBack(item1);
  list.PushBack(item2);
  list.PushBack(item3);

  list.PopFront();
  EXPECT_EQ(list.Front().GetValue(), 2);

  list.PopBack();
  EXPECT_EQ(list.Front().GetValue(), 2);
  EXPECT_EQ(list.Back().GetValue(), 2);

  list.PopFront();
  EXPECT_TRUE(list.Empty());
}

NOLINT_TEST(IntrusiveListTest, IteratorOperations)
{
  IntrusiveList<TestItem> list;
  TestItem item1(1);
  TestItem item2(2);
  TestItem item3(3);

  list.PushBack(item1);
  list.PushBack(item2);
  list.PushBack(item3);

  // Test forward iteration
  auto it = list.begin();
  EXPECT_EQ(it->GetValue(), 1);
  ++it;
  EXPECT_EQ(it->GetValue(), 2);
  // ReSharper disable once CppDFAUnreadVariable (for testing)
  // ReSharper disable once CppDFAUnusedValue
  [[maybe_unused]] auto _ = it++;
  EXPECT_EQ(it->GetValue(), 3);
  ++it;
  EXPECT_EQ(it, list.end());
}

NOLINT_TEST(IntrusiveListTest, RangeBasedFor)
{
  IntrusiveList<TestItem> list;
  TestItem item1(1);
  TestItem item2(2);
  TestItem item3(3);

  list.PushBack(item1);
  list.PushBack(item2);
  list.PushBack(item3);

  int sum = 0;
  for (const auto& item : list) {
    sum += item.GetValue();
  }
  EXPECT_EQ(sum, 6);
}

NOLINT_TEST(IntrusiveListTest, MoveOperations)
{
  IntrusiveList<TestItem> list1;
  TestItem item1(1);
  TestItem item2(2);

  list1.PushBack(item1);
  list1.PushBack(item2);

  IntrusiveList<TestItem> list2 = std::move(list1);
  EXPECT_FALSE(list2.Empty());
  EXPECT_EQ(list2.Front().GetValue(), 1);
  EXPECT_EQ(list2.Back().GetValue(), 2);
}

NOLINT_TEST(IntrusiveListTest, EmptyOperations)
{
  IntrusiveList<TestItem> list;
  list.PopFront(); // Should not crash
  list.PopBack(); // Should not crash
  EXPECT_TRUE(list.Empty());
}

NOLINT_TEST(IntrusiveListTest, ConstAccess)
{
  IntrusiveList<TestItem> list;
  TestItem item1(1);
  list.PushBack(item1);

  const IntrusiveList<TestItem>& const_list = list;
  EXPECT_EQ(const_list.Front().GetValue(), 1);
  EXPECT_EQ(const_list.Back().GetValue(), 1);

  for (const auto& item : const_list) {
    EXPECT_EQ(item.GetValue(), 1);
  }
}
