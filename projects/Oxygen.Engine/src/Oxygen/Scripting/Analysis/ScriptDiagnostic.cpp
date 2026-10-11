//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting {

auto to_string(const DiagnosticSeverity value) noexcept -> std::string_view
{
  switch (value) {
  case DiagnosticSeverity::kError:
    return "error";
  case DiagnosticSeverity::kWarning:
    return "warning";
  case DiagnosticSeverity::kNote:
    return "note";
  }
  return "__NotSupported__";
}

auto to_string(const DiagnosticCode value) -> std::string
{
  return fmt::format("S{:04}", static_cast<uint16_t>(value));
}

auto ScriptDiagnostic::PrimarySpan() const -> const SourceSpan*
{
  const auto it = std::ranges::find_if(
    labels, [](const DiagnosticLabel& label) -> bool { return label.primary; });
  return it == labels.end() ? nullptr : &it->span;
}

auto FormatSummary(const ScriptDiagnostic& diagnostic) -> std::string
{
  auto header = fmt::format("{}[{}]: {}", to_string(diagnostic.severity),
    to_string(diagnostic.code), diagnostic.message);
  const auto* const span = diagnostic.PrimarySpan();
  if (span == nullptr) {
    return header;
  }
  return fmt::format("{}:{}:{}: {}", span->source_name, span->begin.line,
    span->begin.column, header);
}

} // namespace oxygen::scripting
