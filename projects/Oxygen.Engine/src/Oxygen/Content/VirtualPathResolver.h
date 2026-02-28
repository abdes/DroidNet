//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::content {

//! Resolve virtual paths to AssetKey using mounted cooked indexes.
/*!
 A VirtualPathResolver is an editor/tooling helper that maps an editor-facing
 virtual path (e.g. "/.cooked/Materials/Wood.omat") to a runtime-facing
 `data::AssetKey`.

 This resolver is intentionally separate from `AssetLoader`:

 - `AssetLoader` remains keyed by `AssetKey` and is container-agnostic.
 - Virtual path policies (mount points, overrides, prioritization) live above
   the runtime loader.

 Today, resolution is performed by consulting the `container.index.bin` of one
 or more mounted loose cooked roots.

 Resolution can also be backed by mounted `.pak` files when they contain an
 embedded browse index.

 @note This does not load assets. It only resolves identities.
*/
class VirtualPathResolver final {
public:
  OXGN_CNTT_API VirtualPathResolver();
  OXGN_CNTT_API ~VirtualPathResolver();

  OXYGEN_MAKE_NON_COPYABLE(VirtualPathResolver)
  OXYGEN_DEFAULT_MOVABLE(VirtualPathResolver)

  //! Add a loose cooked root in priority order.
  /*!
   Loads and validates the root's `container.index.bin` and registers it for
   subsequent resolutions.

   @param cooked_root Root directory containing `container.index.bin`.
   @throw std::runtime_error If the index cannot be loaded or validated.
  */
  OXGN_CNTT_API auto AddLooseCookedRoot(
    const std::filesystem::path& cooked_root) -> void;

  //! Add a pak file in priority order.
  /*!
   Opens the pak file and registers its embedded browse index (if present) for
   subsequent resolutions.

   @param pak_path Path to the `.pak` file.
   @throw std::runtime_error If the pak cannot be opened.
  */
  OXGN_CNTT_API auto AddPakFile(const std::filesystem::path& pak_path) -> void;

  //! Add a patch pak file and register manifest tombstones.
  /*!
   Validates patch/base compatibility, mounts the patch pak at highest

   * precedence, and applies manifest `deleted` keys as tombstones.

   @param
   * pak_path Path to the patch `.pak` file.
   @param manifest Patch manifest
   * emitted by the cooker.
   @param mounted_base_catalogs Catalog snapshot for
   * the currently mounted base
     set.
   @throw std::runtime_error if
   * compatibility validation fails.
  */
  OXGN_CNTT_API auto AddPatchPakFile(const std::filesystem::path& pak_path,
    const data::PatchManifest& manifest,
    std::span<const data::PakCatalog> mounted_base_catalogs) -> void;

  //! Clear all mounted roots and pak files.
  OXGN_CNTT_API auto ClearMounts() -> void;

  //! Resolve a virtual path to an AssetKey.
  /*!
   Resolution uses runtime patch precedence semantics:
   last-mounted
   * wins with tombstone terminal masking.

   If multiple mounted roots contain
   * the same virtual path but map it to
   different `AssetKey` values, the
   * highest-precedence mapping wins and masked
   lower-priority mappings are
   * logged.

   @param virtual_path Canonical virtual path.
   @return The
   * resolved AssetKey, or `std::nullopt` if not found.
   @throw
   * std::invalid_argument If `virtual_path` is not canonical.
   */
  OXGN_CNTT_NDAPI auto ResolveAssetKey(std::string_view virtual_path) const
    -> std::optional<data::AssetKey>;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content
