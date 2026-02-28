//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::pak {

enum class BuildMode : uint8_t {
  kFull,
  kPatch,
};

OXGN_COOK_NDAPI auto to_string(BuildMode value) noexcept -> std::string_view;

struct PakBuildOptions {
  bool deterministic = true;
  bool embed_browse_index = false;
  bool emit_manifest_in_full = false;
  bool compute_crc32 = true;
  bool fail_on_warnings = false;
};

struct PatchCompatibilityPolicy {
  bool require_exact_base_set = true;
  bool require_content_version_match = true;
  bool require_base_source_key_match = true;
  bool require_catalog_digest_match = true;
};

struct PakBuildRequest {
  BuildMode mode = BuildMode::kFull;
  std::vector<data::CookedSource> sources;

  std::filesystem::path output_pak_path;
  std::filesystem::path output_manifest_path;

  uint16_t content_version = 0;
  data::SourceKey source_key {};

  std::vector<data::PakCatalog> base_catalogs;
  PatchCompatibilityPolicy patch_compat {};
  PakBuildOptions options {};
};

} // namespace oxygen::content::pak
