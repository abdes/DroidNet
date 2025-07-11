//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <chrono>
#include <locale>
#include <regex>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <magic_enum/magic_enum.hpp>

namespace oxygen::clap::detail {

//! Attempts to parse the input string as a signed integer and assigns the
//! result to output
/*!
 @tparam AssignTo Signed integral type to assign to.
 @param input Input string to parse.
 @param output Output variable to assign the parsed value.
 @return True if parsing succeeded and value is in range, false otherwise.
 @throw std::invalid_argument if the input is not a valid unsigned integer.
 @throw std::out_of_range if the value is out of range for AssignTo.

 ### Usage Examples

 ```cpp
 int value;
 bool ok = NumberConversion("42", value);
 ```

 @see UnsignedNumberConversion
*/
template <typename AssignTo>
auto NumberConversion(const std::string& input, AssignTo& output) -> bool
  requires(std::is_integral_v<AssignTo> && std::is_signed_v<AssignTo>)
{
  std::size_t consumed {};
  try {
    const auto output_ll { std::stoll(input, &consumed) };
    output = static_cast<AssignTo>(output_ll);
    return (consumed == input.size()
      && static_cast<std::int64_t>(output) == output_ll);
  } catch (const std::exception&) {
    return false;
  }
}

//! Attempts to parse the input string as an unsigned integer and assigns the
//! result to output
/*!
 @tparam AssignTo Unsigned integral type to assign to.
 @param input Input string to parse.
 @param output Output variable to assign the parsed value.
 @return True if parsing succeeded and value is in range, false otherwise.
 @throw std::invalid_argument if the input is not a valid unsigned integer.
 @throw std::out_of_range if the value is out of range for AssignTo.

 ### Usage Examples

 ```cpp
 unsigned int value;
 bool ok = UnsignedNumberConversion("42", value);
 ```

 @note Unlike std::stoull, this function does not accept a '-' sign in the
 input.
 @see NumberConversion
*/
template <typename AssignTo>
auto UnsignedNumberConversion(const std::string& input, AssignTo& output)
  -> bool
  requires(std::is_integral_v<AssignTo> && std::is_unsigned_v<AssignTo>)
{
  // Contrarily to the behavior od std::stoull, we do not accept a '-' sign
  // in the input
  if (input[0] == '-') {
    return false;
  }
  std::size_t consumed {};
  try {
    const auto output_ull { std::stoull(input, &consumed) };
    output = static_cast<AssignTo>(output_ull);
    return (consumed == input.size()
      && static_cast<std::uint64_t>(output) == output_ull);
  } catch (const std::exception&) {
    return false;
  }
}

//! Attempts to parse the input string as a signed integer and assigns the
//! result to output using NumberConversion.
/*!
 @tparam AssignTo Signed integral type to assign to.
 @param input Input string to parse.
 @param output Output variable to assign the parsed value.
 @return True if parsing succeeded and value is in range, false otherwise.

 ### Usage Examples

 ```cpp
 int value;
 bool ok = ParseValue("42", value);
 ```

 @see NumberConversion
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::is_integral_v<AssignTo> && std::is_signed_v<AssignTo>
    && !std::is_same_v<AssignTo, char> && !std::is_same_v<AssignTo, bool>
    && !std::is_enum_v<AssignTo>)
{
  return NumberConversion(input, output);
}

//! Attempts to parse the input string as an unsigned integer and assigns the
//! result to output using UnsignedNumberConversion.
/*!
 @tparam AssignTo Unsigned integral type to assign to.
 @param input Input string to parse.
 @param output Output variable to assign the parsed value.
 @return True if parsing succeeded and value is in range, false otherwise.

 ### Usage Examples

 ```cpp
 unsigned int value;
 bool ok = ParseValue("42", value);
 ```

 @note Unlike std::stoull, this function does not accept a '-' sign in the
 input.
 @see UnsignedNumberConversion
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::is_integral_v<AssignTo> && std::is_unsigned_v<AssignTo>
    && !std::is_same_v<AssignTo, char> && !std::is_same_v<AssignTo, bool>
    && !std::is_enum_v<AssignTo>)
{
  return UnsignedNumberConversion(input, output);
}

//! Return a lower case version of a string
inline auto ToLower(std::string str) -> std::string
{
  std::ranges::transform(
    str, std::begin(str), [](const std::string::value_type& element) {
      return std::tolower(element, std::locale());
    });
  return str;
}

//! Converts a string representation of a flag or boolean-like value to an
//! integer, typically for binary flag parsing
/*!
 @param val Input string to convert. Accepts numeric, textual, and symbolic
        representations (e.g., "true", "on", "1", "+", "no", "0", "-").
 @return Integer value: 1 for true/positive, -1 for false/negative, or the
         parsed integer value if numeric.
 @throw std::invalid_argument if the input is not recognized as a valid flag.
 @throw std::out_of_range if the numeric value is out of range for int64_t.

 ### Usage Examples

 ```cpp
 auto v1 = StringToFlagValue("true");   // returns 1
 auto v2 = StringToFlagValue("off");    // returns -1
 auto v3 = StringToFlagValue("7");      // returns 7
 auto v4 = StringToFlagValue("-");      // returns -1
 auto v5 = StringToFlagValue("+0");     // returns -1
 ```

 @note Accepts both textual and numeric flag representations. Special handling
       for "+0" and "-0" as false.
 @see ParseValue
*/
inline auto StringToFlagValue(std::string val) -> std::int64_t
{
  val = ToLower(val);
  if (val.size() == 1) {
    if (val[0] >= '1' && val[0] <= '9') {
      return (static_cast<std::int64_t>(val[0]) - '0');
    }
    switch (val[0]) {
    case '0':
    case 'f':
    case 'n':
    case '-':
      return -1;
    case 't':
    case 'y':
    case '+':
      return 1;
    default:
      throw std::invalid_argument("unrecognized character");
    }
  }
  if (val == "true" || val == "on" || val == "yes" || val == "enable") {
    return 1;
  }
  if (val == "false" || val == "off" || val == "no" || val == "disable") {
    return -1;
  }
  // Special handling for +0 and -0: treat as -1 (false), matching boolean logic
  if (val == "+0" || val == "-0") {
    return -1;
  }
  return std::stoll(val);
}

//!  Attempts to parse the input string as a boolean value and assigns the
//!  result to output.
/*!
 @tparam AssignTo Boolean type to assign to (must be `bool`).
 @param input Input string to parse. Accepts numeric, textual, and symbolic
        representations (e.g., "true", "on", "1", "+", "no", "0", "-").
 @param output Output variable to assign the parsed boolean value.
 @return True if parsing succeeded and value is recognized, false otherwise.

 @throw std::invalid_argument if the input is not recognized as a valid flag.
 @throw std::out_of_range if the numeric value is out of range for int64_t.

 ### Usage Examples

 ```cpp
 bool value;
 bool ok = ParseValue("true", value);   // value == true, ok == true
 ok = ParseValue("off", value);         // value == false, ok == true
 ok = ParseValue("0", value);           // value == false, ok == true
 ok = ParseValue("yes", value);         // value == true, ok == true
 ok = ParseValue("invalid", value);     // value unchanged, ok == false
 ```

 @note Accepts both textual and numeric flag representations. Special handling
       for "+0" and "-0" as false.
 @see StringToFlagValue
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::is_same_v<AssignTo, bool>)
{
  try {
    const auto flag_value = StringToFlagValue(input);
    output = (flag_value > 0);
    return true;
  } catch (const std::invalid_argument&) {
    return false;
  } catch (const std::out_of_range&) {
    // if the number is out of the range of a 64 bit value then it is still a
    // number, and all we care about is the sign
    output = (input[0] != '-');
    return true;
  }
}

//! Attempts to parse the input string as a character value and assigns the
//! result to output.
/*!
 @tparam AssignTo Character type to assign to (must be `char` and not an enum).
 @param input Input string to parse. If the string is a single character, that
        character is assigned directly. Otherwise, attempts numeric conversion.
 @param output Output variable to assign the parsed character value.
 @return True if parsing succeeded and value is recognized, false otherwise.

 ### Usage Examples

 ```cpp
 char value;
 bool ok = ParseValue("A", value);   // value == 'A', ok == true
 ok = ParseValue("65", value);       // value == 'A', ok == true (ASCII code)
 ok = ParseValue("foo", value);      // value unchanged, ok == false
 ```

 @note Accepts both single character and numeric string representations.
 @see NumberConversion
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::is_same_v<AssignTo, char> && !std::is_enum_v<AssignTo>)
{
  if (input.size() == 1) {
    output = static_cast<AssignTo>(input[0]);
    return true;
  }
  return NumberConversion(input, output);
}

//! Attempts to parse the input string as a floating-point value and assigns the
//! result to output.
/*!
 @tparam AssignTo Floating-point type to assign to (e.g., `float`, `double`,
         `long double`).
 @param input Input string to parse.
 @param output Output variable to assign the parsed floating-point value.
 @return True if parsing succeeded and the entire input was consumed, false
         otherwise.

 @throw std::invalid_argument if the input is not a valid floating-point number.
 @throw std::out_of_range if the value is out of range for AssignTo.

 ### Usage Examples

 ```cpp
 double value;
 bool ok = ParseValue("3.1415", value);   // value == 3.1415, ok == true
 ok = ParseValue("invalid", value);       // value unchanged, ok == false
 ```

 @see std::stold
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::is_floating_point_v<AssignTo>)
{
  std::size_t consumed {};
  try {
    const auto output_ld { std::stold(input, &consumed) };
    output = static_cast<AssignTo>(output_ld);
    return (consumed == input.size());
  } catch (const std::exception&) {
    return false;
  }
}

//! Attempts to assign the input string directly to the output variable.
/*!
 @tparam AssignTo Type to assign to (must be assignable from `std::string` and
         not an integral or floating-point type).
 @param input Input string to assign.
 @param output Output variable to assign the string value.
 @return Always returns true.

 ### Usage Examples

 ```cpp
 std::string value;
 bool ok = ParseValue("hello", value);   // value == "hello", ok == true
 ```

 @note This overload is selected for types that are assignable from
       `std::string` but are not integral or floating-point types.
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(!std::is_floating_point_v<AssignTo> && !std::is_integral_v<AssignTo>
    && std::is_assignable_v<AssignTo&, std::string>)
{
  output = input;
  return true;
}

//! Attempts to construct the output variable from the input string using the
//! type's string constructor.
/*!
 @tparam AssignTo Type to assign to (must be constructible from `std::string`
         but not assignable from `std::string`, and not an integral or
         floating-point type).
 @param input Input string to construct the value from.
 @param output Output variable to assign the constructed value.
 @return Always returns true.

 ### Usage Examples

 ```cpp
 MyStringLike value;
 bool ok = ParseValue("hello", value);   // value == MyStringLike("hello"), ok
 == true
 ```

 @note This overload is selected for types that are constructible from
       `std::string` but are not assignable from it, and are not integral or
       floating-point types.
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(!std::is_floating_point_v<AssignTo> && !std::is_integral_v<AssignTo>
    && !std::is_assignable_v<AssignTo&, std::string>
    && std::is_constructible_v<AssignTo, std::string>)
{
  output = AssignTo(input);
  return true;
}

//! Attempts to parse the input string as an enumeration value and assigns the
//! result to output.
/*!
 @tparam AssignTo Enumeration type to assign to.
 @param input Input string to parse. Accepts case-insensitive enum names (with
 or without 'k' prefix), exact enum names, or the underlying integer value.
 @param output Output variable to assign the parsed enum value.
 @return True if parsing succeeded and value is recognized, false otherwise.

 ### Usage Examples

 ```cpp
 enum class Color { kRed, kGreen, kBlue };
 Color value;
 bool ok = ParseValue("red", value);     // value == Color::kRed, ok == true
 ok = ParseValue("kGreen", value);       // value == Color::kGreen, ok == true
 ok = ParseValue("2", value);            // value == Color::kBlue, ok == true
 ok = ParseValue("yellow", value);       // value unchanged, ok == false
 ```

 @note Accepts both case-insensitive names (with or without 'k' prefix) and
       integer values for enums.
 @see magic_enum::enum_cast, NumberConversion
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::is_enum_v<AssignTo>)
{
  // Try case-insensitive match for the value name without 'k' prefix
  const std::string lowered = ToLower(input);
  for (const auto& entry : magic_enum::enum_entries<AssignTo>()) {
    std::string_view name = entry.second;
    if (name.size() > 1 && name[0] == 'k') {
      const auto stripped = std::string(name.substr(1));
      if (ToLower(stripped) == lowered) {
        output = entry.first;
        return true;
      }
    }
  }
  // Try case-sensitive match for the full enum name (e.g., 'kRed')
  auto enum_val = magic_enum::enum_cast<AssignTo>(input);
  if (enum_val.has_value()) {
    output = *enum_val;
    return true;
  }
  // Try integer value
  std::underlying_type_t<AssignTo> val;
  if constexpr (std::is_signed_v<std::underlying_type_t<AssignTo>>) {
    if (!NumberConversion(input, val)) {
      return false;
    }
  } else if constexpr (std::is_unsigned_v<std::underlying_type_t<AssignTo>>) {
    if (!UnsignedNumberConversion(input, val)) {
      return false;
    }
  }
  enum_val = magic_enum::enum_cast<AssignTo>(val);
  if (enum_val.has_value()) {
    output = *enum_val;
    return true;
  }
  return false;
}

//! Attempts to parse the input string as a duration value with unit and assigns
//! the result to output.
/*!
 @tparam AssignTo Duration type to assign to (must be a `std::chrono::duration`
         with a floating-point or integral representation).
 @param input Input string to parse. Accepts a number followed by a unit,
        with optional whitespace (e.g., "1.5s", "100 ms", "2 h").
 @param output Output variable to assign the parsed duration value.
 @return True if parsing succeeded and value is recognized, false otherwise.

 ### Usage Examples

 ```cpp
 using namespace std::chrono;
 seconds s;
 bool ok = ParseValue("1.5h", s);    // s == 5400, ok == true
 milliseconds ms;
 ok = ParseValue("250 ms", ms);      // ms == 250, ok == true
 ok = ParseValue("invalid", ms);     // ms unchanged, ok == false
 ```

 @note Supported units: ns, us, ms, s, min, h, d (case-insensitive).
 @warning Fails if the unit is not recognized or the value cannot be parsed.
 @see std::chrono::duration
*/
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::chrono::treat_as_floating_point<typename AssignTo::rep>::value
    || std::is_integral_v<typename AssignTo::rep>)
{
  // Regex: optional whitespace, number (int/float), optional whitespace, unit,
  // optional whitespace
  static const std::regex re(R"(^\s*([+-]?\d*\.?\d+)\s*([a-zA-Z]+)\s*$)",
    std::regex::ECMAScript | std::regex::icase);
  std::smatch match;
  if (!std::regex_match(input, match, re) || match.size() != 3) {
    return false;
  }

  double value;
  try {
    value = std::stod(match[1].str());
  } catch (...) {
    return false;
  }
  std::string unit = match[2].str();
  std::transform(unit.begin(), unit.end(), unit.begin(),
    [](const unsigned char c) { return std::tolower(c); });

  // Convert to seconds as double
  double seconds;
  if (unit == "ns") {
    seconds = value * 1e-9;
  } else if (unit == "us") {
    seconds = value * 1e-6;
  } else if (unit == "ms") {
    seconds = value * 1e-3;
  } else if (unit == "s") {
    seconds = value;
  } else if (unit == "min") {
    seconds = value * 60.0;
  } else if (unit == "h") {
    seconds = value * 3600.0;
  } else if (unit == "d") {
    seconds = value * 86400.0;
  } else {
    return false;
  }

  using namespace std::chrono;
  if constexpr (treat_as_floating_point<typename AssignTo::rep>::value) {
    output = AssignTo(seconds);
  } else {
    output = AssignTo(static_cast<typename AssignTo::rep>(seconds));
  }

  return true;
}

} // namespace oxygen::clap::detail
