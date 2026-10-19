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
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import {
struct ImportRequest;
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::internal {

//! A cooked root a job reads: its own output root or one of its cooked context
//! roots (a loose cooked root or a pak), with its index when it holds one.
struct MountedCookedSource final {
  std::filesystem::path root;
  std::optional<lc::Inspection> inspection;
};

//! Load the cooked roots a job reads, lowest precedence first: the job's
//! output root, then its cooked context roots in order. A root whose index
//! fails to load reports `<diagnostic_prefix>index_load_failed` and is kept
//! without an index.
[[nodiscard]] auto LoadMountedCookedSources(ImportSession& session,
  const ImportRequest& request, std::string_view diagnostic_prefix)
  -> std::vector<MountedCookedSource>;

//! An asset a reference names, found in the mounted cooked roots.
struct ResolvedAssetReference final {
  data::AssetKey key;
  data::AssetType type = data::AssetType::kUnknown;
};

//! Why a reference to a cooked asset is refused.
enum class AssetReferenceRefusal : std::uint8_t {
  //! The reference is not a virtual path the engine accepts.
  kInvalidVirtualPath,
  //! No mounted index lists it, and it is outside the job's mount.
  kUnmounted,
  //! No mounted index lists it, and no mounted root holds its descriptor.
  kMissing,
  //! Its descriptor exists, but its extension names no asset type.
  kTypeUnknown,
  //! It names an asset of another type than the reference expects.
  kTypeMismatch,
};

//! A refused reference, with the type it named when the type was wrong.
struct AssetReferenceRefused final {
  AssetReferenceRefusal reason = AssetReferenceRefusal::kMissing;
  std::optional<data::AssetType> found_type;
};

//! Resolve a reference to a cooked asset by its virtual path.
/*!
 Every authored reference resolves the same way: the mounted indexes are
 searched from the highest precedence down; a path no index lists resolves to
 a descriptor a mounted root already holds at the path's place in the job's
 mount, typed by its extension. The key is the one the index lists, or the
 one the path derives.
*/
[[nodiscard]] auto ResolveMountedAssetReference(
  std::span<const MountedCookedSource> mounts, const ImportRequest& request,
  std::string_view virtual_path, std::optional<data::AssetType> expected_type)
  -> std::expected<ResolvedAssetReference, AssetReferenceRefused>;

} // namespace oxygen::content::import::internal
