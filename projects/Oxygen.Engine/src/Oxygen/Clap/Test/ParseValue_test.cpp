//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Macros.h>
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

//=== Signed Integer Value Parser ===-----------------------------------------//

template <typename T> struct ParseSignedIntegralTest : testing::Test {
  ParseSignedIntegralTest() = default;
  ~ParseSignedIntegralTest() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ParseSignedIntegralTest)
  OXYGEN_MAKE_NON_MOVABLE(ParseSignedIntegralTest)

  struct TestCase {
    const char* input;
    std::optional<T> expected;
  };
  inline static const auto negative
    = TestCase { "-1234", static_cast<T>(-1234) };
  inline static const auto positive = TestCase { "1234", static_cast<T>(1234) };
  inline static const auto error = TestCase { "a23b", {} };
  T value { 0 };
};
using SignedIntegralTypes = testing::Types<int16_t, int32_t, int64_t>;
TYPED_TEST_SUITE(ParseSignedIntegralTest, SignedIntegralTypes, );

NOLINT_TYPED_TEST(ParseSignedIntegralTest, TestPositiveInput)
{
  TypeParam& output = this->value;
  EXPECT_THAT(ParseValue(this->positive.input, output), IsTrue());
  EXPECT_THAT(output, Eq(*this->positive.expected));
}

NOLINT_TYPED_TEST(ParseSignedIntegralTest, TestNegativeInput)
{
  TypeParam& output = this->value;
  EXPECT_THAT(ParseValue(this->negative.input, output), IsTrue());
  EXPECT_THAT(output, Eq(*this->negative.expected));
}

NOLINT_TYPED_TEST(ParseSignedIntegralTest, TestInvalidInput)
{
  TypeParam& output = this->value;
  EXPECT_THAT(ParseValue(this->error.input, output), IsFalse());
}

//=== Unsigned Integer Value Parser ===---------------------------------------//

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
    const char* input;
    std::optional<T> expected;
  };
  inline static const auto negative
    = TestCase { "-1234", static_cast<T>(-1234) };
  inline static const auto positive = TestCase { "1234", static_cast<T>(1234) };
  inline static const auto error = TestCase { "a23b", {} };
  T value { 0 };
};
using UnsignedIntegralTypes = testing::Types<uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(ParseUnsignedIntegralTest, UnsignedIntegralTypes, );

NOLINT_TYPED_TEST(ParseUnsignedIntegralTest, TestPositiveInput)
{
  TypeParam& output = this->value;
  EXPECT_THAT(ParseValue(this->positive.input, output), IsTrue());
  EXPECT_THAT(output, Eq(*this->positive.expected));
}

NOLINT_TYPED_TEST(ParseUnsignedIntegralTest, TestNegativeInput)
{
  TypeParam& output = this->value;
  EXPECT_THAT(ParseValue(this->negative.input, output), IsFalse());
}

NOLINT_TYPED_TEST(ParseUnsignedIntegralTest, TestInvalidInput)
{
  TypeParam& output = this->value;
  EXPECT_THAT(ParseValue(this->error.input, output), IsFalse());
}

//=== Boolean Value Parser ===------------------------------------------------//

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

NOLINT_TEST_P(ParseBooleanTest, ParseWithNoError)
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

NOLINT_TEST_P(ParseBooleanErrorsTest, ParseWithError)
{
  const auto& input = GetParam();
  bool output { true };
  EXPECT_THAT(ParseValue(input, output), IsFalse());
}

//=== Char Value Parser ===---------------------------------------------------//

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

NOLINT_TEST_P(ParseCharTest, ParseWithNoError)
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

NOLINT_TEST_P(ParseCharErrorsTest, ParseWithError)
{
  const auto& input = GetParam();
  char output { 0 };
  EXPECT_THAT(ParseValue(input, output), IsFalse());
}

//=== Floating Point Value Parser ===---------------------------==------------//

struct ParseFloatTest
  : testing::Test,
    testing::WithParamInterface<std::tuple<std::string, double, bool>> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(ValidAndInvalidInputs, ParseFloatTest,
  testing::Values(
    // clang-format off
    std::make_tuple("0.0", 0.0, true),
    std::make_tuple("-1.5", -1.5, true),
    std::make_tuple("3.14159", 3.14159, true),
    std::make_tuple("2.99792458e8", 2.99792458e8, true),
    std::make_tuple("nan", std::numeric_limits<double>::quiet_NaN(), true),
    std::make_tuple("inf", std::numeric_limits<double>::infinity(), true),
    std::make_tuple("-inf", -std::numeric_limits<double>::infinity(), true),
    std::make_tuple("not-a-number", 0.0, false),
    std::make_tuple("", 0.0, false)
    // clang-format on
    ));

NOLINT_TEST_P(ParseFloatTest, ParsesExpectedFloatValue)
{
  const auto& [input, expected, should_succeed] = GetParam();
  double output = 0.0;
  const bool result = ParseValue(input, output);
  EXPECT_THAT(result, Eq(should_succeed));
  if (should_succeed) {
    if (input == "nan") {
      EXPECT_TRUE(std::isnan(output));
    } else if (input == "inf") {
      EXPECT_TRUE(std::isinf(output) && output > 0);
    } else if (input == "-inf") {
      EXPECT_TRUE(std::isinf(output) && output < 0);
    } else {
      EXPECT_THAT(output, Eq(expected));
    }
  }
}

//=== String and String-Constructible Value Parser ===--------------==--------//

struct ParseStringTest : testing::Test { };

NOLINT_TEST_F(ParseStringTest, ParsesStdString)
{
  std::string output;
  EXPECT_THAT(ParseValue("hello world", output), IsTrue());
  EXPECT_THAT(output, Eq("hello world"));
}

struct CustomStringType {
  std::string value;
  explicit CustomStringType(std::string s)
    : value(std::move(s))
  {
  }
  auto operator==(const CustomStringType& other) const -> bool
  {
    return value == other.value;
  }
};

NOLINT_TEST_F(ParseStringTest, ParsesStringConstructibleType)
{
  CustomStringType output { "" };
  EXPECT_THAT(ParseValue("custom", output), IsTrue());
  EXPECT_THAT(output, Eq(CustomStringType { "custom" }));
}

//=== Enum Value Parser ===---------------------------------------------------//

enum class Color : uint8_t { kRed = 1, kGreen = 2, kBlue = 3 };

struct ParseEnumTest
  : testing::Test,
    testing::WithParamInterface<std::tuple<std::string, Color, bool>> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(ValidAndInvalidInputs, ParseEnumTest,
  testing::Values(
    // ReSharper disable StringLiteralTypo
    // clang-format off
    std::make_tuple("red", Color::kRed, true),
    std::make_tuple("green", Color::kGreen, true),
    std::make_tuple("blue", Color::kBlue, true),
    std::make_tuple("1", Color::kRed, true),
    std::make_tuple("2", Color::kGreen, true),
    std::make_tuple("3", Color::kBlue, true),
    std::make_tuple("kRed", Color::kRed, true),
    std::make_tuple("kGreen", Color::kGreen, true),
    std::make_tuple("kBlue", Color::kBlue, true),
    std::make_tuple("kred", Color::kRed, false),
    std::make_tuple("kgreen", Color::kGreen, false),
    std::make_tuple("kblue", Color::kBlue, false),
    std::make_tuple("yellow", Color::kRed, false),
    std::make_tuple("0", Color::kRed, false)
    // clang-format on
    // ReSharper restore StringLiteralTypo
    ));

NOLINT_TEST_P(ParseEnumTest, ParsesExpectedEnumValue)
{
  const auto& [input, expected, should_succeed] = GetParam();
  auto output = Color::kRed;
  const bool result = ParseValue(input, output);
  EXPECT_THAT(result, Eq(should_succeed));
  if (should_succeed) {
    EXPECT_THAT(output, Eq(expected));
  }
}

} // namespace

//=== Chrono Duration Value Parser ===----------------------------------------//

//! Scenario-based tests for ParseValue with std::chrono::duration types.
/*! Covers integer and floating-point durations, valid/invalid units, and edge
 * cases. */
struct ParseChronoDurationTest
  : testing::Test,
    testing::WithParamInterface<
      std::tuple<std::string, double, std::string, bool>> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(ValidAndInvalidInputs, ParseChronoDurationTest,
  testing::Values(
    // ReSharper disable StringLiteralTypo
    // clang-format off
    // input, expected_value, unit, should_succeed
    std::make_tuple("1000ms", 1.0, "s", true),
    std::make_tuple("2.5s", 2.5, "s", true),
    std::make_tuple("3min", 180.0, "s", true),
    std::make_tuple("1h", 3600.0, "s", true),
    std::make_tuple("0.5h", 1800.0, "s", true),
    std::make_tuple("1d", 86400.0, "s", true),
    std::make_tuple("100us", 0.0001, "s", true),
    std::make_tuple("100ns", 0.0000001, "s", true),
    std::make_tuple("42", 0.0, "s", false), // missing unit
    std::make_tuple("10xy", 0.0, "s", false), // invalid unit
    std::make_tuple("abcms", 0.0, "s", false), // invalid number
    std::make_tuple("", 0.0, "s", false)
    // clang-format on
    // ReSharper restore StringLiteralTypo
    ));

NOLINT_TEST_P(ParseChronoDurationTest, ParsesExpectedDuration)
{
  const auto& [input, expected, unit, should_succeed] = GetParam();
  std::chrono::duration<double> output { 0.0 };
  const bool result = ParseValue(input, output);
  EXPECT_THAT(result, Eq(should_succeed));
  if (should_succeed) {
    // Compare in seconds for all units
    EXPECT_NEAR(output.count(), expected, 1e-9);
  }
}

//! Scenario-based tests for ParseValue with integer chrono durations.
/*! Verifies correct parsing and rounding for integral durations. */
struct ParseChronoIntDurationTest
  : testing::Test,
    testing::WithParamInterface<std::tuple<std::string, int64_t, bool>> { };

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(ValidAndInvalidInputs, ParseChronoIntDurationTest,
  testing::Values(
    // ReSharper disable StringLiteralTypo
    // clang-format off
    std::make_tuple("1500ms", 1, true), // rounds down
    std::make_tuple("2000ms", 2, true),
    std::make_tuple("2s", 2, true),
    std::make_tuple("1min", 60, true),
    std::make_tuple("1h", 3600, true),
    std::make_tuple("1d", 86400, true),
    std::make_tuple("0.5h", 1800, true), // fractional hours
    std::make_tuple("10xy", 0, false), // invalid unit
    std::make_tuple("abcms", 0, false), // invalid number
    std::make_tuple("", 0, false)
    // clang-format on
    // ReSharper restore StringLiteralTypo
    ));

NOLINT_TEST_P(ParseChronoIntDurationTest, ParsesExpectedIntDuration)
{
  const auto& [input, expected, should_succeed] = GetParam();
  std::chrono::seconds output { 0 };
  const bool result = ParseValue(input, output);
  EXPECT_THAT(result, Eq(should_succeed));
  if (should_succeed) {
    EXPECT_THAT(output.count(), Eq(expected));
  }
}
