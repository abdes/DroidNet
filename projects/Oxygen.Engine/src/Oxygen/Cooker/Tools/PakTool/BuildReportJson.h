//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuildResult.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>

namespace oxygen::content::pak::tool {

using nlohmann::ordered_json;

struct PakToolRequestSnapshot {
  pak::PakBuildRequest request;
  std::vector<std::filesystem::path> base_catalog_paths;
};

struct PakToolBuildReportInput {
  std::string tool_version;
  std::string command;
  std::string command_line;
  PakToolRequestSnapshot request_snapshot {};
  ArtifactPublicationPlan publication_plan {};
  ArtifactPublicationResult publication_result {};
  pak::PakBuildResult build_result {};
  int exit_code = 0;
  bool success = false;
};

struct ReportWriteResult {
  bool success = false;
  std::string error_code;
  std::string error_message;
};

[[nodiscard]] auto ToReportJson(const PakToolBuildReportInput& input)
  -> ordered_json;

[[nodiscard]] auto ToCanonicalJsonString(const PakToolBuildReportInput& input)
  -> std::string;

[[nodiscard]] auto WriteReportFile(const std::filesystem::path& output_path,
  const PakToolBuildReportInput& input) -> ReportWriteResult;

} // namespace oxygen::content::pak::tool
