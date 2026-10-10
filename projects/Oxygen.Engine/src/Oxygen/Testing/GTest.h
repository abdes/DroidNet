//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <gmock/gmock.h> // IWYU pragma: export
#include <gtest/gtest.h> // IWYU pragma: export

#define NOLINT_TEST(ts, name) TEST(ts, name) // NOLINT
#define NOLINT_TEST_F(ts, name) TEST_F(ts, name) // NOLINT
#define NOLINT_TEST_P(ts, name) TEST_P(ts, name) // NOLINT
#define NOLINT_TYPED_TEST(ts, name) TYPED_TEST(ts, name) // NOLINT
#define NOLINT_ASSERT_THROW(st, ex) ASSERT_THROW(st, ex) // NOLINT
#define NOLINT_EXPECT_THROW(st, ex) EXPECT_THROW(st, ex) // NOLINT
#define NOLINT_ASSERT_NO_THROW(st) ASSERT_NO_THROW(st) // NOLINT
#define NOLINT_EXPECT_NO_THROW(st) EXPECT_NO_THROW(st) // NOLINT
#define NOLINT_ASSERT_DEATH(st, msg) ASSERT_DEATH(st, msg) // NOLINT
#define NOLINT_EXPECT_DEATH(st, msg) EXPECT_DEATH(st, msg) // NOLINT

//! Fatal assertion that a `std::optional` or `std::expected` holds a value.
/*!
 Use it instead of `ASSERT_TRUE(x.has_value())`. GoogleTest routes that
 condition through `testing::AssertionResult`, so
 bugprone-unchecked-optional-access cannot tell that `x` was checked and
 reports every later access. Here `has_value()` is the branch condition and
 the failure returns, which the analysis follows. It has GoogleTest's
 `if (ok) ; else fail` shape, so it is safe as the body of an `if` with an
 `else`, and streams a message: `ASSERT_HAS_VALUE(x) << "context"`.
*/
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define ASSERT_HAS_VALUE(value)                                                \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_                                                \
  if ((value).has_value())                                                     \
    ;                                                                          \
  else                                                                         \
    GTEST_FATAL_FAILURE_(#value " has no value")
// NOLINTEND(cppcoreguidelines-macro-usage)

// A void test-function using ASSERT_ or EXPECT_ calls with a custom message
// should be encapsulated by this macro. Example:
// TRACE_CHECK_F(MyCheckForEquality(counter, 42), "for counter=42").
// NB: The message must be very concise to avoid cluttering the test code.
#define TRACE_GCHECK_F_IMPL(statement, message)                                \
  {                                                                            \
    SCOPED_TRACE(message);                                                     \
    ASSERT_NO_FATAL_FAILURE((statement));                                      \
  }
#define TRACE_GCHECK_F(statement, message)                                     \
  TRACE_GCHECK_F_IMPL(statement, message) // NOLINT
#define GCHECK_F(statement) TRACE_GCHECK_F_IMPL(statement, "") // NOLINT
