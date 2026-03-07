//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <Oxygen/Data/CookedSource.h>

namespace oxygen::content::pak::tool {

struct PakToolOutputOptions {
  bool quiet = false;
  bool no_color = false;
  std::filesystem::path diagnostics_file;
};

struct PakToolRequestOptions {
  std::vector<data::CookedSource> sources;
  std::filesystem::path output_pak;
  std::filesystem::path catalog_output;
  uint16_t content_version = 0;
  std::string source_key;
  bool deterministic = true;
  bool embed_browse_index = false;
  bool compute_crc32 = true;
  bool fail_on_warnings = false;
};

struct PakToolBuildCommandOptions {
  std::filesystem::path manifest_output;
};

struct PakToolPatchCommandOptions {
  std::vector<std::filesystem::path> base_catalogs;
  std::filesystem::path manifest_output;
  bool allow_base_set_mismatch = false;
  bool allow_content_version_mismatch = false;
  bool allow_base_source_key_mismatch = false;
  bool allow_catalog_digest_mismatch = false;
};

struct PakToolCliOptions {
  PakToolOutputOptions output {};
  PakToolRequestOptions request {};
  PakToolBuildCommandOptions build {};
  PakToolPatchCommandOptions patch {};
};

} // namespace oxygen::content::pak::tool
