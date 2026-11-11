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

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Pak/PakTextures.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::pak {

enum class BuildMode : uint8_t {
  kFull,
  kPatch,
};

OXGN_COOK_NDAPI auto to_string(BuildMode value) noexcept -> std::string_view;

//! A build is always deterministic: the same request gives the same bytes.
struct PakBuildOptions {
  bool emit_manifest_in_full = false;
  bool compute_crc32 = true;
  bool fail_on_warnings = false;
};

//! A pak this pak references without containing it.
struct PakDependency {
  //! Its catalog, which names its mount.
  data::PakCatalog catalog;
  //! The textures its browse index lists (`ReadPakTextures`).
  PakTextures textures;
};

struct PakBuildRequest {
  BuildMode mode = BuildMode::kFull;
  std::vector<data::CookedSource> sources;

  std::filesystem::path output_pak_path;
  std::filesystem::path output_manifest_path {};

  uint16_t content_version = 0;
  data::SourceKey source_key {};

  std::vector<data::PakCatalog> base_catalogs;
  //! The textures each base pak's browse index lists (`ReadPakTextures`), in
  //! the order of `base_catalogs`; empty when no base pak holds a texture.
  //! Assets of a patch may reference these by key without containing them; a
  //! patch carries a texture of its sources unless a base holds the same one,
  //! and deletes those its sources no longer hold.
  std::vector<PakTextures> base_textures;
  //! The paks this pak references without containing them, lowest first;
  //! full builds and patches alike.
  std::vector<PakDependency> dependencies;
  PakBuildOptions options {};
  //! Where the sources are read and the pak, manifest and catalog published:
  //! the real file system unless a caller (a test) holds them in memory.
  observer_ptr<cooked::CookerStorage> storage
    = make_observer(&cooked::RealCookerStorage());
};

} // namespace oxygen::content::pak
