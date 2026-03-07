//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>

namespace oxygen::content::pak::tool {

struct PakToolRequestSnapshot {
  pak::PakBuildRequest request;
  std::vector<std::filesystem::path> base_catalog_paths;
};

} // namespace oxygen::content::pak::tool
