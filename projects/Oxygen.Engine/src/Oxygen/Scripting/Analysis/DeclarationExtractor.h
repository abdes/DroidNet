//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <vector>

#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace Luau {
class AstStatBlock;
struct Location;
} // namespace Luau

namespace oxygen::scripting::detail {

//! Declarations found in one parsed chunk.
struct ExtractedDeclarations {
  bool has_declaration = false;
  std::vector<ScriptParamDeclaration> declarations;
};

//! Converts a 0-based Luau location to a 1-based diagnostic span.
auto ToSpan(std::string_view source_name, const Luau::Location& location)
  -> SourceSpan;

//! Extracts the `oxygen.script` declaration of a parsed chunk.
/*!
 Appends one diagnostic per rule violation to `diagnostics`. Declarations are
 returned only when the declaration has no errors.
*/
auto ExtractDeclarations(Luau::AstStatBlock& root, std::string_view source_name,
  std::vector<ScriptDiagnostic>& diagnostics) -> ExtractedDeclarations;

} // namespace oxygen::scripting::detail
