//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Testing/GTest.h>

namespace {

TEST(SpanTest, ReturnsMutableReferenceAndSupportsFixedExtent)
{
  std::array values { 1, 2 };
  oxygen::base::CheckedAt(std::span(values), 1) = 3;
  EXPECT_EQ(values.back(), 3);
}

TEST(SpanTest, SupportsConstAndDynamicExtent)
{
  constexpr std::array values { 1, 2 };
  static_assert(oxygen::base::CheckedAt(std::span(values), 1) == 2);
  const std::span<const int> view(values);
  EXPECT_EQ(&oxygen::base::CheckedAt(view, 0), values.data());
}

TEST(SpanTest, RejectsEmptyPastEndAndOverflowIndices)
{
  const std::array values { 1, 2 };
  EXPECT_THROW(
    static_cast<void>(oxygen::base::CheckedAt(std::span<const int>(), 0)),
    std::out_of_range);
  EXPECT_THROW(static_cast<void>(
                 oxygen::base::CheckedAt(std::span(values), values.size())),
    std::out_of_range);
  EXPECT_THROW(static_cast<void>(oxygen::base::CheckedAt(
                 std::span(values), std::numeric_limits<std::size_t>::max())),
    std::out_of_range);
}

} // namespace
