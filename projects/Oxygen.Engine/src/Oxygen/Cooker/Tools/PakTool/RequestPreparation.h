//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolOptions.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestSnapshot.h>

namespace oxygen::content::pak::tool {

struct RequestPreparationError {
  std::string error_code;
  std::string error_message;
  std::filesystem::path path;
};

struct PreparedPakToolRequest {
  pak::PakBuildRequest build_request;
  PakToolRequestSnapshot request_snapshot {};
  ArtifactPublicationPlan publication_plan {};
};

//! Checks the request's inputs and outputs through `storage`, and plans where
//! its artifacts are staged and published.
[[nodiscard]] auto PreparePakToolRequest(pak::BuildMode mode,
  const PakToolCliOptions& options, const cooked::CookerStorageReader& storage)
  -> Result<PreparedPakToolRequest, RequestPreparationError>;

} // namespace oxygen::content::pak::tool
