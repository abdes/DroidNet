//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Result.h>

#include <cstdint>
#include <string>
#include <type_traits>

#include <Oxygen/Testing/GTest.h>

namespace {

enum class TestError : uint32_t {
  kOne = 1,
  kTwo = 2,
};

//! Test: Result holds and reports a value.
NOLINT_TEST(ResultTest, HoldsValue)
{
  // Arrange
  const oxygen::Result<int, TestError> r = oxygen::Ok(42);

  // Act / Assert
  EXPECT_TRUE(r.has_value());
  EXPECT_FALSE(r.has_error());
  ASSERT_NE(r.value_if(), nullptr);
  EXPECT_EQ(*r.value_if(), 42);
  EXPECT_EQ(r.value(), 42);
}

//! Test: Result holds and reports an error.
NOLINT_TEST(ResultTest, HoldsError)
{
  // Arrange
  oxygen::Result<int, TestError> r = oxygen::Err(TestError::kTwo);

  // Act / Assert
  EXPECT_FALSE(r.has_value());
  EXPECT_TRUE(r.has_error());
  ASSERT_NE(r.error_if(), nullptr);
  EXPECT_EQ(*r.error_if(), TestError::kTwo);
  EXPECT_EQ(r.error(), TestError::kTwo);
}

//! Test: Result::Ok and Result::Err factories work.
NOLINT_TEST(ResultTest, FactoriesWork)
{
  // Arrange / Act
  const auto ok = oxygen::Result<int, TestError>::Ok(7);
  const auto err = oxygen::Result<int, TestError>::Err(TestError::kOne);

  // Assert
  EXPECT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value(), 7);

  EXPECT_FALSE(err.has_value());
  EXPECT_EQ(err.error(), TestError::kOne);
}

//! Test: Err constructor accepts convertible error types.
NOLINT_TEST(ResultTest, ErrConverts)
{
  // Arrange
  const uint32_t raw = 2u;

  // Act
  const oxygen::Result<int, TestError> r
    = oxygen::Err(static_cast<TestError>(raw));

  // Assert
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), TestError::kTwo);
}

//! Test: Ok constructor accepts movable values.
NOLINT_TEST(ResultTest, OkMovesValue)
{
  // Arrange
  std::string s = "hello";

  // Act
  oxygen::Result<std::string, TestError> r = oxygen::Ok(std::move(s));

  // Assert
  EXPECT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), "hello");
}

//! Test: Result<void> success and error behavior.
NOLINT_TEST(ResultTest, VoidResult)
{
  // Arrange
  const oxygen::Result<void, TestError> ok = oxygen::Result<void, TestError>();
  const oxygen::Result<void, TestError> err = oxygen::Err(TestError::kOne);

  // Act / Assert
  EXPECT_TRUE(ok.has_value());
  EXPECT_FALSE(ok.has_error());

  EXPECT_FALSE(err.has_value());
  EXPECT_TRUE(err.has_error());
  ASSERT_NE(err.error_if(), nullptr);
  EXPECT_EQ(*err.error_if(), TestError::kOne);
}

//! Test: Err(std::errc) maps to std::error_code.
NOLINT_TEST(ResultTest, ErrFromErrcIsErgonomic)
{
  // Arrange
  const auto expected = std::make_error_code(std::errc::invalid_argument);

  // Act
  const oxygen::Result<void> r = oxygen::Err(std::errc::invalid_argument);

  // Assert
  EXPECT_FALSE(r.has_value());
  EXPECT_TRUE(r.has_error());
  ASSERT_NE(r.error_if(), nullptr);
  EXPECT_EQ(*r.error_if(), expected);
  EXPECT_EQ(r.error(), expected);
}

//! Test: Result constructors are explicit to avoid ambiguity.
NOLINT_TEST(ResultTest, ConstructorsAreExplicit)
{
  // Arrange / Act / Assert
  static_assert(!std::is_convertible_v<int, oxygen::Result<int, TestError>>);
  static_assert(
    !std::is_convertible_v<TestError, oxygen::Result<int, TestError>>);

  static_assert(std::is_convertible_v<oxygen::OkValue<int>,
    oxygen::Result<int, TestError>>);
  static_assert(std::is_convertible_v<oxygen::ErrValue<TestError>,
    oxygen::Result<int, TestError>>);
}

} // namespace
