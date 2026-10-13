//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

namespace Oxygen::Interop::Scripting {

using namespace System;

//! How deep an analysis goes; each level includes the ones before it.
public
enum class ScriptAnalysisLevel {
  //! Parse and extract the parameter declarations.
  Declarations = 0,
  //! Also run the Luau linter (S04xx).
  Lint = 1,
  //! Also type-check against the Oxygen API definitions (S05xx).
  Types = 2,
};

//! Severity of a script diagnostic.
public
enum class ScriptDiagnosticSeverity {
  Error = 0,
  Warning = 1,
  Note = 2,
};

//! Declared type of a script parameter; values match the engine's
//! `ScriptParamType`.
public
enum class ScriptParameterType {
  None = 0,
  Bool = 1,
  Int32 = 2,
  Float = 3,
  String = 4,
  Vec2 = 5,
  Vec3 = 6,
  Vec4 = 7,
  Asset = 8,
  Node = 9,
};

//! Source range of a diagnostic: 1-based lines and columns, end exclusive.
public
value struct ScriptSourceSpan {
  String ^ SourceName;
  int Line;
  int Column;
  int EndLine;
  int EndColumn;
};

//! One script problem reported by the engine analyzer.
public
ref class ScriptDiagnosticInfo sealed {
public:
  property ScriptDiagnosticSeverity Severity;
  //! Code as written in diagnostics, for example `S0503`.
  property String ^ Code;
  //! One line, lower case, no trailing period.
  property String ^ Message;
  //! The code's one-line title from the diagnostic catalog.
  property String ^ Title;
  //! What the code means and how to resolve it, from the diagnostic catalog.
  property String ^ Explanation;
  //! Primary span, when the diagnostic has one.
  property Nullable<ScriptSourceSpan> PrimarySpan;
  //! One-line form: `source:line:column: error[S0503]: message`.
  property String ^ Summary;
  //! Full multi-line rendering with source excerpts, labels, notes and help,
  //! in the engine's layout.
  property String ^ Rendered;
};

//! One parameter a script declares, with its authoring hints.
public
ref class ScriptParameterInfo sealed {
public:
  property String ^ Name;
  property ScriptParameterType Type;
  //! Authored default: Boolean, Int32, Single, String, Vector2, Vector3 or
  //! Vector4; null for asset and node parameters.
  property Object ^ DefaultValue;
  //! Native asset type of an asset parameter (`kind`), 0 otherwise.
  property Byte AssetType;
  property Nullable<double> Min;
  property Nullable<double> Max;
  property Nullable<double> Step;
  property String ^ Unit;
  property String ^ Description;
  property array<String ^> ^ Options;
  property bool IsColor;
  //! Line of the parameter name in its declaration.
  property int Line;
  property int Column;
};

//! Result of one analysis.
public
ref class ScriptAnalysisResult sealed {
public:
  //! True when the script contains an `oxygen.script` declaration.
  property bool HasDeclaration;
  //! Declared parameters in declaration order; empty when the declaration
  //! has errors.
  property array<ScriptParameterInfo ^> ^ Parameters;
  property array<ScriptDiagnosticInfo ^> ^ Diagnostics;
};

//! Reads a script's declarations and problems without running it, through
//! the engine's `ScriptAnalyzer`. Stateless and thread-safe; it does not need
//! a running engine.
public
ref class ScriptAnalysis abstract sealed {
public:
  //! Analyzes one script.
  /*!
   @param sourceName Name used in diagnostic spans (project-relative path).
   @param source The script's UTF-8 bytes.
   @param level How deep the analysis goes.
   @param typeDefinitions Text of `oxygen.d.luau`, or null; without it
     `Types` runs as `Lint`.
  */
  static auto Analyze(String ^ sourceName, array<Byte> ^ source,
    ScriptAnalysisLevel level, String ^ typeDefinitions)
    -> ScriptAnalysisResult ^;
};

} // namespace Oxygen::Interop::Scripting

#pragma managed(pop)
