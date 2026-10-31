//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <concepts>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

namespace oxygen::cooker::test {

//! Any diagnostic record with `code` and `message` members, such as
//! `ImportDiagnostic` and `PakDiagnostic`.
template <typename T>
concept DiagnosticRecord = requires(const T& d) {
  { d.code } -> std::convertible_to<std::string_view>;
  { d.message } -> std::convertible_to<std::string_view>;
};

template <std::ranges::input_range R>
  requires DiagnosticRecord<std::ranges::range_value_t<R>>
[[nodiscard]] auto HasDiagnosticCode(
  const R& diagnostics, std::string_view code) -> bool
{
  return std::ranges::any_of(
    diagnostics, [code](const auto& d) -> auto { return d.code == code; });
}

//! One `"<code>: <message>"` line per diagnostic, for failure messages.
template <std::ranges::input_range R>
  requires DiagnosticRecord<std::ranges::range_value_t<R>>
[[nodiscard]] auto DiagnosticSummary(const R& diagnostics) -> std::string
{
  auto out = std::ostringstream {};
  for (const auto& d : diagnostics) {
    out << d.code << ": " << d.message << "\n";
  }
  return out.str();
}

//! A diagnostic record with an error severity, such as `PakDiagnostic` and
//! `ImportDiagnostic`.
template <typename T>
concept SeverityDiagnostic = DiagnosticRecord<T> && requires(const T& d) {
  { d.severity == decltype(d.severity)::kError } -> std::convertible_to<bool>;
};

template <std::ranges::input_range R>
  requires SeverityDiagnostic<std::ranges::range_value_t<R>>
[[nodiscard]] auto HasErrorDiagnostic(const R& diagnostics) -> bool
{
  return std::ranges::any_of(diagnostics, [](const auto& d) -> bool {
    return d.severity == decltype(d.severity)::kError;
  });
}

//! Matches diagnostics without an error; a failure prints all of them.
/*!
 ```cpp
 ASSERT_THAT(result.diagnostics, HasNoErrors());
 ```
*/
MATCHER(HasNoErrors, "has no error diagnostic")
{
  if (!HasErrorDiagnostic(arg)) {
    return true;
  }
  *result_listener << "whose diagnostics are\n" << DiagnosticSummary(arg);
  return false;
}

//! Matches diagnostics holding one with `code`; a failure prints all of them.
MATCHER_P(HasDiagnostic, code,
  std::string(negation ? "has no" : "has a") + " diagnostic "
    + std::string(code))
{
  if (HasDiagnosticCode(arg, code)) {
    return true;
  }
  *result_listener << "whose diagnostics are\n" << DiagnosticSummary(arg);
  return false;
}

} // namespace oxygen::cooker::test
