//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>

#include <Oxygen/Data/SourceKey.h>

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>

namespace oxygen::content::import {

//! Request for importing a source file into a loose cooked container.
struct ImportRequest final {
  //! Source file (FBX, glTF, or GLB).
  std::filesystem::path source_path;

  //! Optional destination directory (the loose cooked root).
  /*!
   If set, this path MUST be absolute.

   If unset, the importer uses the source file's directory
   (`source_path.parent_path()`) as the cooked root.
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

  //! Import options.
  ImportOptions options = {};
};

} // namespace oxygen::content::import
