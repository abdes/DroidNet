//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ScriptSource.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::content::import {

auto ToImportSeverity(const scripting::DiagnosticSeverity severity)
  -> ImportSeverity
{
  switch (severity) {
  case scripting::DiagnosticSeverity::kError:
    return ImportSeverity::kError;
  case scripting::DiagnosticSeverity::kWarning:
    return ImportSeverity::kWarning;
  case scripting::DiagnosticSeverity::kNote:
    break;
  }
  return ImportSeverity::kInfo;
}

auto AnalyzeScriptDeclarations(const std::span<const std::byte> source,
  const std::string_view source_name, const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<std::vector<scripting::ScriptParamDeclaration>>
{
  auto analysis = scripting::ScriptAnalyzer {}.Analyze({
    .source_name = source_name,
    .source = source,
    .level = scripting::AnalysisLevel::kDeclarations,
  });
  for (const auto& diagnostic : analysis.diagnostics) {
    diagnostics.push_back({
      .severity = ToImportSeverity(diagnostic.severity),
      .code = std::string(import_codes::kScriptCodePrefix)
        + scripting::to_string(diagnostic.code),
      .message = scripting::FormatSummary(diagnostic),
      .source_path = source_path.string(),
    });
  }
  if (analysis.HasErrors()) {
    return std::nullopt;
  }

  // A name or default a descriptor cannot hold is found here, not when the
  // descriptor is written.
  auto strings = data::DescriptorStringsBuilder {};
  for (const auto& declaration : analysis.declarations) {
    if (!data::MakeScriptParamRecord(
          declaration.name, declaration.default_value, strings)) {
      diagnostics.push_back({
        .severity = ImportSeverity::kError,
        .code = std::string(import_codes::kScriptAssetDeclarationUnencodable),
        .message = "Parameter '" + declaration.name
          + "' has an empty name or holds a NUL byte",
        .source_path = source_path.string(),
      });
      return std::nullopt;
    }
  }
  return std::move(analysis.declarations);
}

} // namespace oxygen::content::import
