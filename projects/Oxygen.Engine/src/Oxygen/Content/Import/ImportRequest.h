//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

//! Supported authoring source formats.
enum class ImportFormat : uint8_t {
  kUnknown = 0,
  kGltf,
  kFbx,
  kTextureImage,
};

OXGN_CNTT_NDAPI auto to_string(ImportFormat format) -> std::string_view;

//! A single source mapping for multi-source imports.
struct ImportSource final {
  std::filesystem::path path;
  SubresourceId subresource;
};

//! Request for importing a source file into a loose cooked container.
struct ImportRequest final {
  //! Source file (FBX, glTF, GLB, or primary texture).
  std::filesystem::path source_path;

  //! Optional additional source files for multi-source imports.
  std::vector<ImportSource> additional_sources;

  //! Optional destination directory (the loose cooked root).
  /*!
   If set, this path MUST be absolute.

    If unset, the importer derives the cooked root from `source_path` and
    `loose_cooked_layout.virtual_mount_root`, ensuring the cooked root ends with
    the virtual mount root leaf directory (by default: `.cooked`).
  */
  std::optional<std::filesystem::path> cooked_root;

  //! Loose cooked container layout conventions.
  /*!
   Destination container layout used by the cook pipeline.

   This controls where the importer should place descriptor files for
    different asset types (scene/geometry/materials) and where it
   should write bulk resource blobs (tables/data).

   @note To place all descriptors into a single folder, set `descriptors_dir`
    to that folder and set all *\*_subdir fields (for example, `scenes_subdir`,
    `geometry_subdir`, `materials_subdir`) to empty strings.
  */
  LooseCookedLayout loose_cooked_layout = {};

  //! Optional explicit source GUID for the cooked container.
  std::optional<data::SourceKey> source_key;

  //! Optional human-readable job name for logging and UI.
  std::optional<std::string> job_name;

  //! Import options.
  ImportOptions options = {};

  //! Derives a stable scene name from the source file stem.
  /*!
   Used as the default namespace for imported assets and for scene virtual path
   generation. Returns "Scene" if the source path has no stem.
  */
  OXGN_CNTT_NDAPI auto GetSceneName() const -> std::string;

  //! Auto-detects the import format from the source path extension.
  OXGN_CNTT_NDAPI auto GetFormat() const -> ImportFormat;
};

} // namespace oxygen::content::import
