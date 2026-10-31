//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestSnapshot.h>
#include <Oxygen/Cooker/Tools/PakTool/StagedSourceStorage.h>

namespace oxygen::content::pak::tool {

struct ScriptSealingError {
  std::string error_code;
  std::string error_message;
  std::filesystem::path source_path {};
  std::filesystem::path descriptor_path {};
  std::filesystem::path resolved_path {};
  std::string external_source_path;
};

struct ScriptSealingResult {
  //! The request, with every sealed loose source replaced by its staged root.
  pak::PakBuildRequest build_request {};
  std::vector<std::filesystem::path> staged_loose_roots;
  //! Every file sealing published: what cleanup removes.
  std::vector<std::filesystem::path> staged_files;
  std::uint32_t sealed_script_assets { 0U };
};

//! Seals the external script sources of the request's loose sources.
/*!
 A loose source that needs sealing is staged under `staging_parent` in
 `storage`: the staged root holds the sealed script descriptors, the script
 tables and the index, and shows every other file of the original root. The
 pak build reads the returned request's sources through `storage`.
*/
[[nodiscard]] auto SealLooseCookedSourcesForPakBuild(
  const pak::PakBuildRequest& build_request,
  const std::filesystem::path& staging_parent,
  const SourceMountFolders& source_mounts, StagedSourceStorage& storage)
  -> Result<ScriptSealingResult, ScriptSealingError>;

//! Removes `staged_files` from `storage`, then every staging folder they
//! leave empty; a failure is logged and the rest are still removed.
auto CleanupStagedFiles(cooked::CookerStorage& storage,
  std::span<const std::filesystem::path> staged_files) -> void;

} // namespace oxygen::content::pak::tool
