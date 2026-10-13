//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Graphics/Common/Types/IndexRange.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::graphics::IndexRange;

namespace {

NOLINT_TEST(IndexRange, DefaultConstructionIsEmpty)
{
  IndexRange range;
  EXPECT_EQ(range.BaseIndex(), 0U);
  EXPECT_EQ(range.Count(), 0U);
  EXPECT_FALSE(range.Contains(0));
}

NOLINT_TEST(IndexRange, ConstructWithBaseAndCount)
{
  IndexRange range(10, 5);
  EXPECT_EQ(range.BaseIndex(), 10U);
  EXPECT_EQ(range.Count(), 5U);
  EXPECT_TRUE(range.Contains(10));
  EXPECT_TRUE(range.Contains(14));
  EXPECT_FALSE(range.Contains(15));
  EXPECT_FALSE(range.Contains(9));
}

NOLINT_TEST(IndexRange, ZeroCountIsAlwaysEmpty)
{
  IndexRange range(42, 0);
  EXPECT_FALSE(range.Contains(42));
  EXPECT_FALSE(range.Contains(41));
}

NOLINT_TEST(IndexRange, IsEmptyReturnsTrueForZeroCount)
{
  IndexRange empty1;
  EXPECT_TRUE(empty1.IsEmpty());
  IndexRange empty2(100, 0);
  EXPECT_TRUE(empty2.IsEmpty());
  IndexRange nonempty(5, 2);
  EXPECT_FALSE(nonempty.IsEmpty());
}

NOLINT_TEST(IndexRange, StaticEmptyFactoryProducesEmptyRange)
{
  auto empty = IndexRange::Empty();
  EXPECT_TRUE(empty.IsEmpty());
  EXPECT_EQ(empty.BaseIndex(), 0U);
  EXPECT_EQ(empty.Count(), 0U);
}

NOLINT_TEST(IndexRange, EndIndexIsBasePlusCount)
{
  IndexRange r(5, 3);
  EXPECT_EQ(r.EndIndex(), 8U);
  EXPECT_EQ(r.BaseIndex(), 5U);
  EXPECT_EQ(r.Count(), 3U);
}

NOLINT_TEST(IndexRange, EqualityAndInequalityOperators)
{
  IndexRange a(1, 2);
  IndexRange b(1, 2);
  IndexRange c(2, 2);
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
  EXPECT_TRUE(a != c);
}

NOLINT_TEST(IndexRange, SwapExchangesContents)
{
  IndexRange a(1, 2);
  IndexRange b(3, 4);
  a.swap(b);
  EXPECT_EQ(a.BaseIndex(), 3U);
  EXPECT_EQ(a.Count(), 4U);
  EXPECT_EQ(b.BaseIndex(), 1U);
  EXPECT_EQ(b.Count(), 2U);
}

NOLINT_TEST(IndexRange, StdSwapUsesCustomSwap)
{
  IndexRange a(7, 2);
  IndexRange b(11, 5);
  std::swap(a, b);
  EXPECT_EQ(a.BaseIndex(), 11U);
  EXPECT_EQ(a.Count(), 5U);
  EXPECT_EQ(b.BaseIndex(), 7U);
  EXPECT_EQ(b.Count(), 2U);
}

} // anonymous namespace
