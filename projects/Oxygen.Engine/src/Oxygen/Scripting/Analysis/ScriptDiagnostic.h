//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Scripting/Analysis/api_export.h>

namespace oxygen::scripting {

//! Severity of a script diagnostic.
enum class DiagnosticSeverity : uint8_t {
  kError = 0,
  kWarning = 1,
  kNote = 2,
};
OXGN_SCAN_NDAPI auto to_string(DiagnosticSeverity value) noexcept
  -> std::string_view;

//! Stable script diagnostic code; the value is the number after `S`.
/*!
 Codes are grouped by family: S01xx syntax, S03xx declarations. Retired codes
 are never reused.
*/
enum class DiagnosticCode : uint16_t {
  // Syntax
  kSyntaxError = 101,

  // Declarations
  kDeclarationNotTopLevelLocal = 301,
  kValueNotLiteral = 302,
  kDuplicateDeclaration = 303,
  kDeclarationNotReturned = 304,
  kDeclarationNotTable = 305,
  kUnknownField = 306,
  kParamNotDeclared = 307,
  kUnknownParamType = 308,
  kInvalidParamName = 309,
  kDuplicateParam = 310,
  kDefaultTypeMismatch = 311,
  kFieldNotApplicable = 312,
  kInvalidAssetKind = 313,
  kValueTooLong = 314,
  kInvalidRange = 315,
  kFieldTypeMismatch = 316,
};

//! Returns the code as written in diagnostics, for example `S0302`.
OXGN_SCAN_NDAPI auto to_string(DiagnosticCode value) -> std::string;

//! 1-based line and 1-based column in UTF-8 bytes.
struct SourcePosition {
  uint32_t line = 1;
  uint32_t column = 1;

  auto operator==(const SourcePosition&) const -> bool = default;
};

//! Source range of a diagnostic; `end` is exclusive.
struct SourceSpan {
  std::string source_name; //!< Virtual or project-relative path.
  SourcePosition begin;
  SourcePosition end;

  auto operator==(const SourceSpan&) const -> bool = default;
};

//! A span with an optional message drawn under it.
struct DiagnosticLabel {
  SourceSpan span;
  std::string message;
  //! Exactly one label is primary when any span is known.
  bool primary = false;
};

//! Help attached to a diagnostic, optionally machine-applicable.
struct Suggestion {
  std::string message;
  std::optional<SourceSpan> span; //!< Replaced range, if applicable.
  std::optional<std::string> replacement;
};

//! One script problem, from any producer, in renderer-neutral form.
struct ScriptDiagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::kError;
  DiagnosticCode code = DiagnosticCode::kSyntaxError;
  //! One line, lower case, no trailing period.
  std::string message;
  std::vector<DiagnosticLabel> labels;
  std::vector<std::string> notes;
  std::optional<Suggestion> help;

  //! Returns the primary label's span, if any.
  OXGN_SCAN_NDAPI auto PrimarySpan() const -> const SourceSpan*;
};

//! Formats a one-line summary: `source:line:column: error[S0302]: message`.
/*!
 Used where only a single line fits, such as cook reports and logs. The full
 multi-line rendering belongs to the diagnostic renderer.
*/
OXGN_SCAN_NDAPI auto FormatSummary(const ScriptDiagnostic& diagnostic)
  -> std::string;

} // namespace oxygen::scripting
