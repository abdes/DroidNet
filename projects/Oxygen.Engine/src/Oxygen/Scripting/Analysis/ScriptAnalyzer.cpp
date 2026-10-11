//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include <Luau/Allocator.h>
#include <Luau/Lexer.h>
#include <Luau/ParseResult.h>
#include <Luau/Parser.h>

#include <Oxygen/Scripting/Analysis/DeclarationExtractor.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting {

auto ScriptAnalysis::HasErrors() const noexcept -> bool
{
  return std::ranges::any_of(
    diagnostics, [](const ScriptDiagnostic& diagnostic) -> bool {
      return diagnostic.severity == DiagnosticSeverity::kError;
    });
}

auto ScriptAnalyzer::Analyze(const ScriptAnalysisRequest& request) const
  -> ScriptAnalysis
{
  auto source = std::string(request.source.size(), '\0');
  if (!request.source.empty()) {
    std::memcpy(source.data(), request.source.data(), request.source.size());
  }

  auto allocator = Luau::Allocator {};
  auto names = Luau::AstNameTable(allocator);
  const auto parsed
    = Luau::Parser::parse(source.data(), source.size(), names, allocator);

  auto analysis = ScriptAnalysis {};
  if (!parsed.errors.empty()) {
    // A chunk with syntax errors has no reliable declaration.
    for (const auto& error : parsed.errors) {
      analysis.diagnostics.push_back(
        detail::ToSyntaxDiagnostic(request.source_name, error));
    }
    return analysis;
  }

  auto extracted = detail::ExtractDeclarations(
    *parsed.root, request.source_name, analysis.diagnostics);
  analysis.has_declaration = extracted.has_declaration;
  analysis.declarations = std::move(extracted.declarations);
  return analysis;
}

} // namespace oxygen::scripting
