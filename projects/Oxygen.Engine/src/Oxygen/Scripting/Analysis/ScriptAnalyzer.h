//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Analysis/api_export.h>

namespace oxygen::scripting {

//! How deep a script analysis goes.
enum class AnalysisLevel : uint8_t {
  //! Parse and extract the `oxygen.script` parameter declarations.
  kDeclarations = 0,
};

//! One parameter a script declares, with its authoring hints.
/*!
 `default_value` holds the alternative of the declared type. It is the
 authored default, or the type's zero value when none is authored. Asset and
 node parameters have no default: an asset value carries only its declared
 kind (`ScriptAssetParam::asset_type`) with a nil key, and a node value is
 unset.

 Hints are tools data: they shape editor fields and are not cooked.
*/
struct ScriptParamDeclaration {
  std::string name;
  data::pak::scripting::ScriptParamType type
    = data::pak::scripting::ScriptParamType::kNone;
  data::ScriptParam default_value;

  std::optional<double> min;
  std::optional<double> max;
  std::optional<double> step;
  std::string unit;
  std::string description;
  std::vector<std::string> options;
  bool color = false;

  //! Span of the parameter name in its declaration.
  SourceSpan span;
};

//! Input of one script analysis.
struct ScriptAnalysisRequest {
  //! Virtual or project-relative path used in diagnostic spans.
  std::string_view source_name;
  std::span<const std::byte> source;
  AnalysisLevel level = AnalysisLevel::kDeclarations;
};

//! Result of one script analysis.
struct ScriptAnalysis {
  //! True when the script contains an `oxygen.script` declaration. A script
  //! without one (a module other scripts load) declares no parameters.
  bool has_declaration = false;
  //! Declared parameters, in declaration order. Empty when the declaration
  //! has errors.
  std::vector<ScriptParamDeclaration> declarations;
  std::vector<ScriptDiagnostic> diagnostics;

  //! True when any diagnostic is an error.
  OXGN_SCAN_NDAPI auto HasErrors() const noexcept -> bool;
};

//! Reads a script's declarations and problems without running it.
/*!
 Parses Luau source with the Luau AST and validates the script's
 `oxygen.script { params = { ... } }` declaration against the declaration
 rules: one top-level declaration assigned to a local the chunk returns,
 literal field values only, and types, defaults and hints that agree. Every
 violation is a diagnostic with a source span. The cooker, the compile
 service and the editor use this one analyzer, so all of them report the
 same declarations and diagnostics.

 The analyzer is stateless and safe to use from several threads.
*/
class ScriptAnalyzer final {
public:
  //! Largest parameter name, in bytes (ScriptParamRecord key capacity - 1).
  static constexpr std::size_t kMaxParamNameBytes = 63;
  //! Largest string default, in bytes (ScriptParamRecord value capacity - 1).
  static constexpr std::size_t kMaxStringValueBytes = 59;

  //! Analyzes one script.
  OXGN_SCAN_NDAPI auto Analyze(const ScriptAnalysisRequest& request) const
    -> ScriptAnalysis;
};

} // namespace oxygen::scripting
