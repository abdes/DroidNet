//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuildResult.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/Tools/PakTool/BuildReportJson.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestPreparation.h>

namespace oxygen::content::pak::tool {

enum class PakToolExitCode : int {
  kSuccess = 0,
  kUsageError = 1,
  kPreparationFailure = 2,
  kBuildFailure = 3,
  kRuntimeFailure = 4,
};

struct PakToolCommandResult {
  PakToolExitCode exit_code = PakToolExitCode::kRuntimeFailure;
  std::string error_code;
  std::string error_message;
  std::filesystem::path error_path;
  ArtifactPublicationPlan publication_plan {};
  ArtifactPublicationResult publication_result {};
  pak::PakBuildResult build_result {};
  ReportWriteResult report_write_result {};
};

[[nodiscard]] auto ExecutePakToolCommand(const pak::BuildMode mode,
  std::string command, std::string command_line, std::string tool_version,
  const PakToolCliOptions& options, IRequestPreparationFileSystem& prep_fs,
  IArtifactFileSystem& artifact_fs) -> PakToolCommandResult;

} // namespace oxygen::content::pak::tool
