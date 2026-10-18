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
 Codes are grouped by family (see `DiagnosticFamily`): the hundreds digit
 names the family. Every code has an entry in the diagnostic catalog. Retired
 codes are never reused.
*/
enum class DiagnosticCode : uint16_t {
  // Syntax
  kSyntaxError = 101,

  // Compile
  kCompileError = 201,
  kNoCompiler = 202,
  kCompileStopped = 203,
  kEmptySource = 204,

  // Declarations
  kDeclarationNotTopLevelLocal = 301,
  kValueNotLiteral = 302,
  kDuplicateDeclaration = 303,
  kDeclarationNotReturned = 304,
  kDeclarationNotTable = 305,
  kUnknownField = 306,
  kParamNotDeclared = 307,
  kUnknownParamType = 308,
  kDuplicateParam = 310,
  kDefaultTypeMismatch = 311,
  kFieldNotApplicable = 312,
  kInvalidAssetKind = 313,
  kInvalidRange = 315,
  kFieldTypeMismatch = 316,

  // Lint: 400 plus the Luau lint code
  kLintUnknown = 400,
  kLintUnknownGlobal = 401,
  kLintDeprecatedGlobal = 402,
  kLintGlobalUsedAsLocal = 403,
  kLintLocalShadow = 404,
  kLintSameLineStatement = 405,
  kLintMultiLineStatement = 406,
  kLintLocalUnused = 407,
  kLintFunctionUnused = 408,
  kLintImportUnused = 409,
  kLintBuiltinGlobalWrite = 410,
  kLintPlaceholderRead = 411,
  kLintUnreachableCode = 412,
  kLintUnknownType = 413,
  kLintForRange = 414,
  kLintUnbalancedAssignment = 415,
  kLintImplicitReturn = 416,
  kLintDuplicateLocal = 417,
  kLintFormatString = 418,
  kLintTableLiteral = 419,
  kLintUninitializedLocal = 420,
  kLintDuplicateFunction = 421,
  kLintDeprecatedApi = 422,
  kLintTableOperations = 423,
  kLintDuplicateCondition = 424,
  kLintMisleadingAndOr = 425,
  kLintCommentDirective = 426,
  kLintIntegerParsing = 427,
  kLintComparisonPrecedence = 428,
  kLintRedundantNativeAttribute = 429,

  // Types
  kTypeMismatch = 501,
  kUnknownGlobal = 502,
  kUnknownMember = 503,
  kArgumentCountMismatch = 504,
  kNotCallable = 505,
  kOptionalValueAccess = 506,
  kDeprecatedApi = 507,
  kUnsupportedRequire = 508,
  kTypeError = 509,
  kTypeCheckIncomplete = 510,

  // Binding and attachment
  kNotAScript = 601,
  kLoadFailed = 602,
  kAttachFailed = 603,
  kOverrideRejected = 604,

  // Runtime
  kRuntimeError = 701,
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

  auto operator==(const DiagnosticLabel&) const -> bool = default;
};

//! Help attached to a diagnostic, optionally machine-applicable.
struct Suggestion {
  std::string message;
  std::optional<SourceSpan> span; //!< Replaced range, if applicable.
  std::optional<std::string> replacement;

  auto operator==(const Suggestion&) const -> bool = default;
};

//! The script slot a binding or runtime diagnostic belongs to.
struct SlotContext {
  std::string node; //!< Name of the node that owns the slot.
  uint32_t slot_index = 0; //!< Zero-based slot position on the node.
  std::string hook; //!< Hook or stage, such as `on_gameplay` or `load`.

  auto operator==(const SlotContext&) const -> bool = default;
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
  //! Set for binding and runtime diagnostics of a script slot.
  std::optional<SlotContext> slot;

  //! Returns the primary label's span, if any.
  OXGN_SCAN_NDAPI auto PrimarySpan() const -> const SourceSpan*;

  auto operator==(const ScriptDiagnostic&) const -> bool = default;
};

//! Formats a one-line summary: `source:line:column: error[S0302]: message`.
/*!
 A slot diagnostic ends with its context, as in `(in on_gameplay, node "Rig",
 slot 1)`. Used where only a single line fits, such as cook reports and logs.
 The full multi-line rendering belongs to the diagnostic renderer.
*/
OXGN_SCAN_NDAPI auto FormatSummary(const ScriptDiagnostic& diagnostic)
  -> std::string;

} // namespace oxygen::scripting
