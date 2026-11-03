//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>

namespace oxygen::content::import {

//! The import severity of an analysis diagnostic; notes are information.
OXGN_COOK_NDAPI auto ToImportSeverity(scripting::DiagnosticSeverity severity)
  -> ImportSeverity;

//! Reads the parameter declarations of a script's source; no cooked output.
/*!
 Reports every analysis diagnostic as `script.<code>`, with the severity the
 analyzer gave it, and refuses a script that has errors or declares a
 parameter a descriptor cannot encode (`script.asset.declaration_unencodable`).
 The diagnostics are not located in a JSON document: the analyzer's own spans
 are in their messages.

 @param source The script's source bytes.
 @param source_name The path diagnostics name the script by.
 @param source_path The source path the diagnostics report.
 @param diagnostics Receives every diagnostic.
 @return The declarations in declaration order, or nothing when an error was
 added.
*/
OXGN_COOK_NDAPI auto AnalyzeScriptDeclarations(
  std::span<const std::byte> source, std::string_view source_name,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<std::vector<scripting::ScriptParamDeclaration>>;

} // namespace oxygen::content::import
