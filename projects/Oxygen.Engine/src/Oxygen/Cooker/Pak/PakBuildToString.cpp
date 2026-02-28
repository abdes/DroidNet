//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/Pak/PakBuildPhase.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>

namespace oxygen::content::pak {

auto to_string(const BuildMode value) noexcept -> std::string_view
{
  switch (value) {
  case BuildMode::kFull:
    return "Full";
  case BuildMode::kPatch:
    return "Patch";
  }

  return "__NotSupported__";
}

auto to_string(const PakDiagnosticSeverity value) noexcept -> std::string_view
{
  switch (value) {
  case PakDiagnosticSeverity::kInfo:
    return "Info";
  case PakDiagnosticSeverity::kWarning:
    return "Warning";
  case PakDiagnosticSeverity::kError:
    return "Error";
  }

  return "__NotSupported__";
}

auto to_string(const PakBuildPhase value) noexcept -> std::string_view
{
  switch (value) {
  case PakBuildPhase::kRequestValidation:
    return "RequestValidation";
  case PakBuildPhase::kPlanning:
    return "Planning";
  case PakBuildPhase::kWriting:
    return "Writing";
  case PakBuildPhase::kManifest:
    return "Manifest";
  case PakBuildPhase::kFinalize:
    return "Finalize";
  }

  return "__NotSupported__";
}

} // namespace oxygen::content::pak
