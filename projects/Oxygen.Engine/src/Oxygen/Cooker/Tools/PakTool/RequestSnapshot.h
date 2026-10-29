//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>

namespace oxygen::content::pak::tool {

//! Source mount folders by mount name, as external scripts name them.
using SourceMountFolders
  = std::map<std::string, std::filesystem::path, std::less<>>;

struct PakToolRequestSnapshot {
  pak::PakBuildRequest request;
  std::vector<std::filesystem::path> base_pak_paths;
  std::vector<std::filesystem::path> dependency_pak_paths;
  //! The bound source mounts external scripts are sealed from.
  SourceMountFolders source_mounts;
};

} // namespace oxygen::content::pak::tool
