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

template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::is_integral_v<AssignTo> && std::is_signed_v<AssignTo>
    && !std::is_same_v<AssignTo, char> && !std::is_same_v<AssignTo, bool>
    && !std::is_enum_v<AssignTo>)
{
  return NumberConversion(input, output);
}

template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(std::is_integral_v<AssignTo> && std::is_unsigned_v<AssignTo>
    && !std::is_same_v<AssignTo, char> && !std::is_same_v<AssignTo, bool>
    && !std::is_enum_v<AssignTo>)
{
  return UnsignedNumberConversion(input, output);
}

/// Return a lower case version of a string
inline auto ToLower(std::string str) -> std::string
{
  std::ranges::transform(
    str, std::begin(str), [](const std::string::value_type& element) {
      return std::tolower(element, std::locale());
    });
  return str;
}

/// Convert a flag into an integer value typically binary flags
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

/// String and similar direct assignment
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(!std::is_floating_point_v<AssignTo> && !std::is_integral_v<AssignTo>
    && std::is_assignable_v<AssignTo&, std::string>)
{
  output = input;
  return true;
}

/// String and similar constructible and copy assignment
template <typename AssignTo>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
  requires(!std::is_floating_point_v<AssignTo> && !std::is_integral_v<AssignTo>
    && !std::is_assignable_v<AssignTo&, std::string>
    && std::is_constructible_v<AssignTo, std::string>)
{
  output = AssignTo(input);
  return true;
}

/// Enumerations
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
  if (NumberConversion(input, val)) {
    enum_val = magic_enum::enum_cast<AssignTo>(val);
    if (enum_val.has_value()) {
      output = *enum_val;
      return true;
    }
  }
  return false;
}

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
