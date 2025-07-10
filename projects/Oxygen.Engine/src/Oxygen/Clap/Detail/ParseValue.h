//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <locale>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <magic_enum/magic_enum.hpp>

namespace asap::clap::detail {

template <typename AssignTo,
  std::enable_if_t<std::is_integral_v<AssignTo> && std::is_signed_v<AssignTo>,
    std::nullptr_t>
  = nullptr>
auto NumberConversion(const std::string& input, AssignTo& output) -> bool
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

template <typename AssignTo,
  std::enable_if_t<std::is_integral_v<AssignTo> && std::is_unsigned_v<AssignTo>,
    std::nullptr_t>
  = nullptr>
auto UnsignedNumberConversion(const std::string& input, AssignTo& output)
  -> bool
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

template <typename AssignTo,
  std::enable_if_t<std::is_integral_v<AssignTo> && std::is_signed_v<AssignTo>
      && !std::is_same_v<AssignTo, char> && !std::is_same_v<AssignTo, bool>
      && !std::is_enum_v<AssignTo>,
    std::nullptr_t>
  = nullptr>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
{
  return NumberConversion(input, output);
}

template <typename AssignTo,
  std::enable_if_t<std::is_integral_v<AssignTo> && std::is_unsigned_v<AssignTo>
      && !std::is_same_v<AssignTo, char> && !std::is_same_v<AssignTo, bool>
      && !std::is_enum_v<AssignTo>,
    std::nullptr_t>
  = nullptr>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
{
  return UnsignedNumberConversion(input, output);
}

/// Return a lower case version of a string
inline auto ToLower(std::string str) -> std::string
{
  std::transform(std::begin(str), std::end(str), std::begin(str),
    [](const std::string::value_type& element) {
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
  return std::stoll(val);
}

template <typename AssignTo,
  std::enable_if_t<std::is_same_v<AssignTo, bool>, std::nullptr_t> = nullptr>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
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

template <typename AssignTo,
  std::enable_if_t<std::is_same_v<AssignTo, char> && !std::is_enum_v<AssignTo>,
    std::nullptr_t>
  = nullptr>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
{
  if (input.size() == 1) {
    output = static_cast<AssignTo>(input[0]);
    return true;
  }
  return NumberConversion(input, output);
}

/// Floats (TODO: TEST)
template <typename AssignTo,
  std::enable_if_t<std::is_floating_point_v<AssignTo>, std::nullptr_t>
  = nullptr>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
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
template <typename AssignTo,
  std::enable_if_t<!std::is_floating_point_v<AssignTo>
      && !std::is_integral_v<AssignTo>
      && std::is_assignable_v<AssignTo&, std::string>,
    std::nullptr_t>
  = nullptr>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
{
  output = input;
  return true;
}

/// String and similar constructible and copy assignment
template <typename AssignTo,
  std::enable_if_t<!std::is_floating_point_v<AssignTo>
      && !std::is_integral_v<AssignTo>
      && !std::is_assignable_v<AssignTo&, std::string>
      && std::is_constructible_v<AssignTo, std::string>,
    std::nullptr_t>
  = nullptr>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
{
  output = AssignTo(input);
  return true;
}

/// Enumerations
template <typename AssignTo,
  std::enable_if_t<std::is_enum_v<AssignTo>, std::nullptr_t> = nullptr>
auto ParseValue(const std::string& input, AssignTo& output) -> bool
{
  // first try to parse for an enum name
  auto enum_val = magic_enum::enum_cast<AssignTo>(ToLower(input));
  if (!enum_val.has_value()) {
    // maybe it's an integer value then
    std::underlying_type_t<AssignTo> val;
    if (!NumberConversion(input, val)) {
      return false;
    }
    enum_val = magic_enum::enum_cast<AssignTo>(val);
    if (!enum_val.has_value()) {
      return false;
    }
  }
  output = *enum_val;
  return true;
}

} // namespace asap::clap::detail
