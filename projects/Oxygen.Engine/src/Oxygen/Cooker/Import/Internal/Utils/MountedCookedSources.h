//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import {
struct ImportRequest;
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::internal {

//! A cooked root a job reads: its own output root, a cooked source of its
//! source's mount table or one of its cooked context roots (a loose cooked
//! root or a pak), with its content when it is a cooked source that opens.
struct MountedCookedSource final {
  std::filesystem::path root;
  std::optional<cooked::CookedContent> content;
};

//! Load the cooked roots a job reads (`BuildMountedCookedRoots`), loose
//! cooked roots and paks, lowest precedence first. A root whose content fails
//! to open reports `<diagnostic_prefix>index_load_failed` and is kept without
//! content.
[[nodiscard]] auto LoadMountedCookedSources(ImportSession& session,
  const ImportRequest& request, std::string_view diagnostic_prefix)
  -> std::vector<MountedCookedSource>;

//! An asset a reference names, found in the mounted cooked roots.
struct ResolvedAssetReference final {
  data::AssetKey key;
  data::AssetType type = data::AssetType::kUnknown;
};

struct ResolvedReference;

//! Why a reference to a cooked asset is refused.
enum class AssetReferenceRefusal : std::uint8_t {
  //! No mounted index lists it, and it is outside the job's mount.
  kUnmounted,
  //! It is in the job's mount, and no mounted index lists it.
  kMissing,
  //! It names an asset of another type than the reference expects.
  kTypeMismatch,
};

//! A refused reference, with the type it named when the type was wrong.
struct AssetReferenceRefused final {
  AssetReferenceRefusal reason = AssetReferenceRefusal::kMissing;
  std::optional<data::AssetType> found_type;
  //! Where each mounted index searched would hold the descriptor, highest
  //! precedence first; empty for a reference outside the job's mount.
  std::vector<std::filesystem::path> looked_up;
};

//! Look up the cooked asset a resolved reference names.
/*!
 Every authored reference is looked up the same way, after
 `ResolveJobReference` resolved it: the mounted indexes are searched from the
 highest precedence down; a path no index lists resolves to a descriptor a
 mounted root already holds at the path's place in the job's mount, typed by
 its extension. The key is the one the index lists, or the one the path
 derives.
*/
[[nodiscard]] auto LookupReference(std::span<const MountedCookedSource> mounts,
  const ImportRequest& request, const ResolvedReference& reference,
  std::optional<data::AssetType> expected_type)
  -> std::expected<ResolvedAssetReference, AssetReferenceRefused>;

//! The sentence that says what a refused lookup looked for, for a diagnostic.
/*!
 Names the written and resolved forms and, for a missing descriptor, each file
 looked for: `'../Textures/Brick.otex' resolves to /Content/Textures/
 Brick.otex; looked for <root>/Textures/Brick.otex`.
*/
OXGN_COOK_NDAPI auto DescribeLookup(const ResolvedReference& reference,
  const AssetReferenceRefused& refused) -> std::string;

} // namespace oxygen::content::import::internal
