//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/Types.h

#include <cstdint>
#include <limits>
#include <system_error>
#include <type_traits>

#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteRange;

constexpr auto kMax = std::numeric_limits<uint64_t>::max();

//! Offsets and counts are distinct types: neither converts to the other.
static_assert(!std::is_convertible_v<ByteOffset, ByteCount>);
static_assert(!std::is_convertible_v<ByteCount, ByteOffset>);
static_assert(!std::is_convertible_v<uint64_t, ByteOffset>);

//! An offset plus a count is an offset; an offset minus an offset is a count.
NOLINT_TEST(ByteOffsetTest, ArithmeticKeepsUnits)
{
  auto offset = ByteOffset { 10 } + ByteCount { 5 };
  offset += ByteCount { 3 };
  const auto distance = offset - ByteOffset { 4 };

  static_assert(std::is_same_v<decltype(distance), const ByteCount>);
  EXPECT_EQ(offset.get(), 18U);
  EXPECT_EQ(distance.get(), 14U);
}

//! A range whose end is representable is made; one past UINT64_MAX is not.
NOLINT_TEST(ByteRangeTest, MakeRejectsUnrepresentableEnd)
{
  const auto fits = ByteRange::Make(ByteOffset { kMax - 4 }, ByteCount { 4 });
  const auto overflows
    = ByteRange::Make(ByteOffset { kMax - 4 }, ByteCount { 5 });

  ASSERT_TRUE(fits);
  EXPECT_EQ(fits->End().get(), kMax);
  ASSERT_FALSE(overflows);
  EXPECT_EQ(overflows.error(), std::errc::value_too_large);
}

//! Containment is inclusive of both ends; an empty range at the end is
//! contained, one past it is not.
NOLINT_TEST(ByteRangeTest, ContainsHonoursBothEnds)
{
  const auto range = ByteRange {
    .offset = ByteOffset { 100 },
    .count = ByteCount { 50 },
  };
  const auto make = [](const uint64_t offset, const uint64_t count) {
    return ByteRange {
      .offset = ByteOffset { offset },
      .count = ByteCount { count },
    };
  };

  EXPECT_TRUE(range.Contains(range));
  EXPECT_TRUE(range.Contains(make(120, 30)));
  EXPECT_TRUE(range.Contains(make(150, 0)));
  EXPECT_FALSE(range.Contains(make(151, 0)));
  EXPECT_FALSE(range.Contains(make(99, 10)));
  EXPECT_FALSE(range.Contains(make(140, 11)));
  EXPECT_FALSE(range.Contains(make(120, kMax)));
}

} // namespace
