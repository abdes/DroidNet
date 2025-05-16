//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define NOLINT_TEST(ts, name) TEST(ts, name) // NOLINT
#define NOLINT_TEST_F(ts, name) TEST_F(ts, name) // NOLINT
#define NOLINT_TEST_P(ts, name) TEST_P(ts, name) // NOLINT
#define NOLINT_TYPED_TEST(ts, name) TYPED_TEST(ts, name) // NOLINT
#define NOLINT_ASSERT_THROW(st, ex) ASSERT_THROW(st, ex) // NOLINT
#define NOLINT_EXPECT_THROW(st, ex) EXPECT_THROW(st, ex) // NOLINT
#define NOLINT_ASSERT_NO_THROW(st) ASSERT_NO_THROW(st) // NOLINT
#define NOLINT_EXPECT_NO_THROW(st) EXPECT_NO_THROW(st) // NOLINT
