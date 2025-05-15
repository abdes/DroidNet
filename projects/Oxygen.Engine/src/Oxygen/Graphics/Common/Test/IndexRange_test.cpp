//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Types/IndexRange.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::graphics::IndexRange;

namespace {

NOLINT_TEST(IndexRange, DefaultConstructionIsEmpty) {
    IndexRange range;
    EXPECT_EQ(range.BaseIndex(), 0u);
    EXPECT_EQ(range.Count(), 0u);
    EXPECT_FALSE(range.Contains(0));
}

NOLINT_TEST(IndexRange, ConstructWithBaseAndCount) {
    IndexRange range(10, 5);
    EXPECT_EQ(range.BaseIndex(), 10u);
    EXPECT_EQ(range.Count(), 5u);
    EXPECT_TRUE(range.Contains(10));
    EXPECT_TRUE(range.Contains(14));
    EXPECT_FALSE(range.Contains(15));
    EXPECT_FALSE(range.Contains(9));
}

NOLINT_TEST(IndexRange, ZeroCountIsAlwaysEmpty) {
    IndexRange range(42, 0);
    EXPECT_FALSE(range.Contains(42));
    EXPECT_FALSE(range.Contains(41));
}

NOLINT_TEST(IndexRange, IsEmptyReturnsTrueForZeroCount) {
    IndexRange empty1;
    EXPECT_TRUE(empty1.IsEmpty());
    IndexRange empty2(100, 0);
    EXPECT_TRUE(empty2.IsEmpty());
    IndexRange nonempty(5, 2);
    EXPECT_FALSE(nonempty.IsEmpty());
}

NOLINT_TEST(IndexRange, StaticEmptyFactoryProducesEmptyRange) {
    auto empty = IndexRange::Empty();
    EXPECT_TRUE(empty.IsEmpty());
    EXPECT_EQ(empty.BaseIndex(), 0u);
    EXPECT_EQ(empty.Count(), 0u);
}

NOLINT_TEST(IndexRange, EndIndexIsBasePlusCount) {
    IndexRange r(5, 3);
    EXPECT_EQ(r.EndIndex(), 8u);
    EXPECT_EQ(r.BaseIndex(), 5u);
    EXPECT_EQ(r.Count(), 3u);
}

NOLINT_TEST(IndexRange, EqualityAndInequalityOperators) {
    IndexRange a(1, 2);
    IndexRange b(1, 2);
    IndexRange c(2, 2);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

NOLINT_TEST(IndexRange, SwapExchangesContents) {
    IndexRange a(1, 2);
    IndexRange b(3, 4);
    a.swap(b);
    EXPECT_EQ(a.BaseIndex(), 3u);
    EXPECT_EQ(a.Count(), 4u);
    EXPECT_EQ(b.BaseIndex(), 1u);
    EXPECT_EQ(b.Count(), 2u);
}

NOLINT_TEST(IndexRange, StdSwapUsesCustomSwap) {
    IndexRange a(7, 2);
    IndexRange b(11, 5);
    std::swap(a, b);
    EXPECT_EQ(a.BaseIndex(), 11u);
    EXPECT_EQ(a.Count(), 5u);
    EXPECT_EQ(b.BaseIndex(), 7u);
    EXPECT_EQ(b.Count(), 2u);
}

} // anonymous namespace
