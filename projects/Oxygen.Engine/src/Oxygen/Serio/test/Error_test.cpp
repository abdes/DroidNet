//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/Error.h, Serio/Error.cpp

#include <array>
#include <string>
#include <system_error>

#include <Oxygen/Serio/Error.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::Error;
using oxygen::serio::ErrorCategory;

constexpr auto kAllErrors = std::array {
  Error::kEndOfData,
  Error::kOutOfBounds,
  Error::kTooLarge,
  Error::kDigestMismatch,
  Error::kNotFound,
  Error::kAccessDenied,
  Error::kSharingViolation,
  Error::kNoSpace,
  Error::kIo,
};

//! One Serio error value per test.
class SerioErrorValueTest : public ::testing::TestWithParam<Error> { };

INSTANTIATE_TEST_SUITE_P(AllErrors, SerioErrorValueTest,
  ::testing::ValuesIn(kAllErrors),
  [](const ::testing::TestParamInfo<Error>& info) {
    return std::to_string(static_cast<int>(info.param));
  });

//! The value is a code in Serio's category, with a message.
NOLINT_TEST_P(SerioErrorValueTest, IsCodeInSerioCategory)
{
  const auto code = make_error_code(GetParam());

  EXPECT_EQ(&code.category(), &ErrorCategory());
  EXPECT_FALSE(code.message().empty());
}

//! The code equals its own condition and no other.
NOLINT_TEST_P(SerioErrorValueTest, EqualsOnlyItsOwnCondition)
{
  const auto code = make_error_code(GetParam());

  EXPECT_THAT(
    kAllErrors, ::testing::Each(::testing::Truly([&](const Error condition) {
      return (code == condition) == (condition == GetParam());
    })));
}

//! A generic code without a Serio meaning matches no condition.
NOLINT_TEST_P(SerioErrorValueTest, UnrelatedGenericCodeDoesNotMatch)
{
  EXPECT_NE(std::make_error_code(std::errc::invalid_argument), GetParam());
}

//! Generic codes that mean a Serio condition compare equal to it.
NOLINT_TEST(SerioErrorTest, GenericCodesMatchTheirConditions)
{
  EXPECT_EQ(std::make_error_code(std::errc::no_such_file_or_directory),
    Error::kNotFound);
  EXPECT_EQ(
    std::make_error_code(std::errc::permission_denied), Error::kAccessDenied);
  EXPECT_EQ(
    std::make_error_code(std::errc::no_space_on_device), Error::kNoSpace);
  EXPECT_EQ(std::make_error_code(std::errc::io_error), Error::kIo);
}

} // namespace
