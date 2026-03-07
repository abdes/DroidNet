//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>

namespace oxygen::content::pak::tool {

struct ScriptSealingError {
  std::string error_code;
  std::string error_message;
  std::filesystem::path source_path;
  std::filesystem::path descriptor_path;
  std::filesystem::path resolved_path;
  std::string external_source_path;
};

struct ScriptSealingResult {
  pak::PakBuildRequest build_request {};
  std::vector<std::filesystem::path> staged_loose_roots {};
  uint32_t sealed_script_assets = 0;
};

[[nodiscard]] auto SealLooseCookedSourcesForPakBuild(
  const pak::PakBuildRequest& build_request,
  const std::filesystem::path& staging_parent)
  -> Result<ScriptSealingResult, ScriptSealingError>;

auto CleanupStagedLooseRoots(
  std::span<const std::filesystem::path> staged_loose_roots) -> void;

} // namespace oxygen::content::pak::tool
