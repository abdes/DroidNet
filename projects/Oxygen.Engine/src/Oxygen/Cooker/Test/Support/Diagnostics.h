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

} // namespace oxygen::cooker::test
