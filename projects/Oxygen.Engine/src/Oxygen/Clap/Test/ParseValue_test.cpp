//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Detail/ParseValue.h>

using testing::Eq;
using testing::IsFalse;
using testing::IsTrue;

using oxygen::clap::detail::ParseValue;
using oxygen::clap::detail::StringToFlagValue;
using oxygen::clap::detail::ToLower;

namespace {

class ParseValueBasicTest : public testing::Test { };

//=== ToLower ===-------------------------------------------------------------//

NOLINT_TEST_F(ParseValueBasicTest, ToLower)
{
  EXPECT_THAT(ToLower("True Enable DisABLe"), Eq("true enable disable"));
}

//=== StringToFlagValue ===---------------------------------------------------//

//! Scenario-based tests for StringToFlagValue utility.
/*! Tests all supported flag string inputs, including numeric, symbolic, and
   word forms. Covers normal, edge, and error scenarios. */
struct StringToFlagValueTest
  : testing::Test,
    testing::WithParamInterface<std::pair<std::string, std::int64_t>> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(ValidInputs, StringToFlagValueTest,
  testing::Values(
    // clang-format off
    std::make_pair("1", 1),
    std::make_pair("9", 9),
    std::make_pair("0", -1),
    std::make_pair("-", -1),
    std::make_pair("+", 1),
    std::make_pair("t", 1),
    std::make_pair("T", 1),
    std::make_pair("f", -1),
    std::make_pair("F", -1),
    std::make_pair("y", 1),
    std::make_pair("Y", 1),
    std::make_pair("n", -1),
    std::make_pair("N", -1),
    std::make_pair("true", 1),
    std::make_pair("TRUE", 1),
    std::make_pair("false", -1),
    std::make_pair("FALSE", -1),
    std::make_pair("on", 1),
    std::make_pair("off", -1),
    std::make_pair("yes", 1),
    std::make_pair("no", -1),
    std::make_pair("enable", 1),
    std::make_pair("disable", -1),
    std::make_pair("+1", 1),
    std::make_pair("-0", -1),
    std::make_pair("+0", -1),
    std::make_pair("9223372036854775807", 9223372036854775807ll),
    std::make_pair("-9223372036854775807", -9223372036854775807ll)
    // clang-format on
    ));

NOLINT_TEST_P(StringToFlagValueTest, ParsesExpectedFlagValue)
{
  const auto& [input, expected] = GetParam();
  EXPECT_THAT(StringToFlagValue(input), Eq(expected));
}

//! Scenario-based tests for StringToFlagValue error handling.
/*! Verifies that invalid inputs throw std::invalid_argument. */
struct StringToFlagValueErrorTest : testing::Test,
                                    testing::WithParamInterface<std::string> {
};

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(InvalidInputs, StringToFlagValueErrorTest,
  testing::Values(
    // clang-format off
    "xyz",
    "not",
    "yes!",
    "x",
    "*",
    "++"
    // clang-format on
    ));

NOLINT_TEST_P(StringToFlagValueErrorTest, ThrowsInvalidArgument)
{
  const auto& input = GetParam();
  EXPECT_THROW(StringToFlagValue(input), std::invalid_argument);
}

//=== Typed Value Parsers ===-------------------------------------------------//

template <typename T>
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
struct ParseSignedIntegralTest : testing::Test {
  ParseSignedIntegralTest() = default;

  ParseSignedIntegralTest(const ParseSignedIntegralTest&) = delete;
  ParseSignedIntegralTest(ParseSignedIntegralTest&&) = delete;

  auto operator=(const ParseSignedIntegralTest&)
    -> ParseSignedIntegralTest& = delete;
  auto operator=(ParseSignedIntegralTest&&)
    -> ParseSignedIntegralTest& = delete;

  ~ParseSignedIntegralTest() override = default;

  struct TestCase {
    const char* input_;
    std::optional<T> expected_;
  };
  inline static const auto negative_
    = TestCase { "-1234", static_cast<T>(-1234) };
  inline static const auto positive_
    = TestCase { "1234", static_cast<T>(1234) };
  inline static const auto error_ = TestCase { "a23b", {} };
  T value_ { 0 };
};
using SignedIntegralTypes = testing::Types<int16_t, int32_t, int64_t>;
TYPED_TEST_SUITE(ParseSignedIntegralTest, SignedIntegralTypes, );

// NOLINTNEXTLINE
TYPED_TEST(ParseSignedIntegralTest, TestPositiveInput)
{
  TypeParam& output = this->value_;
  EXPECT_THAT(ParseValue(this->positive_.input_, output), IsTrue());
  EXPECT_THAT(output, Eq(this->positive_.expected_.value()));
}

// NOLINTNEXTLINE
TYPED_TEST(ParseSignedIntegralTest, TestNegativeInput)
{
  TypeParam& output = this->value_;
  EXPECT_THAT(ParseValue(this->negative_.input_, output), IsTrue());
  EXPECT_THAT(output, Eq(this->negative_.expected_.value()));
}

// NOLINTNEXTLINE
TYPED_TEST(ParseSignedIntegralTest, TestInvalidInput)
{
  TypeParam& output = this->value_;
  EXPECT_THAT(ParseValue(this->error_.input_, output), IsFalse());
}

// -----------------------------------------------------------------------------

template <typename T>
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
struct ParseUnsignedIntegralTest : testing::Test {
  ParseUnsignedIntegralTest() = default;

  ParseUnsignedIntegralTest(const ParseUnsignedIntegralTest&) = delete;
  ParseUnsignedIntegralTest(ParseUnsignedIntegralTest&&) = delete;

  auto operator=(const ParseUnsignedIntegralTest&)
    -> ParseUnsignedIntegralTest& = delete;
  auto operator=(ParseUnsignedIntegralTest&&)
    -> ParseUnsignedIntegralTest& = delete;

  ~ParseUnsignedIntegralTest() override = default;

  struct TestCase {
    const char* input_;
    std::optional<T> expected_;
  };
  inline static const auto negative_
    = TestCase { "-1234", static_cast<T>(-1234) };
  inline static const auto positive_
    = TestCase { "1234", static_cast<T>(1234) };
  inline static const auto error_ = TestCase { "a23b", {} };
  T value_ { 0 };
};
using UnsignedIntegralTypes = testing::Types<uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(ParseUnsignedIntegralTest, UnsignedIntegralTypes, );

// NOLINTNEXTLINE
TYPED_TEST(ParseUnsignedIntegralTest, TestPositiveInput)
{
  TypeParam& output = this->value_;
  EXPECT_THAT(ParseValue(this->positive_.input_, output), IsTrue());
  EXPECT_THAT(output, Eq(this->positive_.expected_.value()));
}

// NOLINTNEXTLINE
TYPED_TEST(ParseUnsignedIntegralTest, TestNegativeInput)
{
  TypeParam& output = this->value_;
  EXPECT_THAT(ParseValue(this->negative_.input_, output), IsFalse());
}

// NOLINTNEXTLINE
TYPED_TEST(ParseUnsignedIntegralTest, TestInvalidInput)
{
  TypeParam& output = this->value_;
  EXPECT_THAT(ParseValue(this->error_.input_, output), IsFalse());
}

// -----------------------------------------------------------------------------

struct ParseBooleanTest
  : testing::Test,
    testing::WithParamInterface<std::pair<std::string, bool>> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(ValidInputValues, ParseBooleanTest,
  testing::Values(
    // clang-format off
        std::make_pair("true", true),
        std::make_pair("TRUE", true),
        std::make_pair("on", true),
        std::make_pair("yes", true),
        std::make_pair("enable", true),
        std::make_pair("Enable", true),
        std::make_pair("t", true),
        std::make_pair("y", true),
        std::make_pair("+", true),
        std::make_pair("+1", true),
        std::make_pair("184467440737095516150", true),
        // false
        std::make_pair("false", false),
        std::make_pair("off", false),
        std::make_pair("no", false),
        std::make_pair("disable", false),
        std::make_pair("disABLE", false),
        std::make_pair("0", false),
        std::make_pair("f", false),
        std::make_pair("f", false),
        std::make_pair("-", false),
        std::make_pair("+0", false),
        std::make_pair("-0", false),
        std::make_pair("-184467440737095516150", false)
        )); // clang-format on

// NOLINTNEXTLINE
TEST_P(ParseBooleanTest, ParseWithNoError)
{
  const auto& [input, expected] = GetParam();
  bool output { false };
  EXPECT_THAT(ParseValue(input, output), IsTrue());
  EXPECT_THAT(output, Eq(expected));
}

struct ParseBooleanErrorsTest : testing::Test,
                                testing::WithParamInterface<std::string> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(InvalidInputValues, ParseBooleanErrorsTest,
  testing::Values(
    // clang-format off
        "xyz",
        "not",
        "yes!",
        "x",
        "*",
        "++")
    ); // clang-format on

// NOLINTNEXTLINE
TEST_P(ParseBooleanErrorsTest, ParseWithError)
{
  const auto input = GetParam();
  bool output { true };
  EXPECT_THAT(ParseValue(input, output), IsFalse());
}

// -----------------------------------------------------------------------------

struct ParseCharTest
  : testing::Test,
    testing::WithParamInterface<std::pair<std::string, char>> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(ValidInputValues, ParseCharTest,
  testing::Values(
    // clang-format off
        std::make_pair("A", 'A'),
        std::make_pair("-", '-'),
        std::make_pair("65", static_cast<char>(65)),
        std::make_pair("127", static_cast<char>(127))
    )); // clang-format on

// NOLINTNEXTLINE
TEST_P(ParseCharTest, ParseWithNoError)
{
  const auto& [input, expected] = GetParam();
  char output { 0 };
  EXPECT_THAT(ParseValue(input, output), IsTrue());
  EXPECT_THAT(output, Eq(expected));
}

struct ParseCharErrorsTest : testing::Test,
                             testing::WithParamInterface<std::string> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(InvalidInputValues, ParseCharErrorsTest,
  testing::Values(
    // clang-format off
        "xyz",
        "240",
        "1234")
    ); // clang-format on

// NOLINTNEXTLINE
TEST_P(ParseCharErrorsTest, ParseWithError)
{
  const auto& input = GetParam();
  char output { 0 };
  EXPECT_THAT(ParseValue(input, output), IsFalse());
}

} // namespace
