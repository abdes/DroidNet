//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

struct PakCatalogEntry final {
  AssetKey asset_key;
  AssetType asset_type = AssetType::kUnknown;
  std::array<uint8_t, 32> descriptor_digest {};
  std::array<uint8_t, 32> transitive_resource_digest {};
};

//! One required physical layer, in increasing precedence order.
struct PakCatalogBase final {
  SourceKey source_key {};
  uint16_t content_version = 0;
  base::Sha256Digest catalog_digest {};

  auto operator==(const PakCatalogBase&) const -> bool = default;
};

//! An asset or a texture a pak references and another library provides.
/*!
 An asset requirement names the asset's key and type; a texture requirement
 names the texture key (`TextureKeyFromVirtualPath`) and type
 `AssetType::kUnknown`. Logical and physics resource references never leave
 their pak, so they are no requirements.
*/
struct PakRequirement final {
  KeyReferenceKind kind = KeyReferenceKind::kAsset;
  AssetKey key;
  AssetType type = AssetType::kUnknown;

  auto operator==(const PakRequirement&) const -> bool = default;
};

//! A library a pak depends on, by its name, and what the pak requires of it.
struct PakCatalogDependency final {
  //! The library's one root, such as `Engine`.
  std::string library;
  std::vector<PakRequirement> requirements;

  auto operator==(const PakCatalogDependency&) const -> bool = default;
};

struct PakCatalog final {
  SourceKey source_key {};
  uint16_t content_version = 0;
  std::array<uint8_t, 32> catalog_digest {};
  //! The pak's one root name: every entry's virtual path lies under it.
  std::string library;
  std::vector<PakCatalogEntry> entries;
  std::vector<AssetKey> deleted;
  //! Patch baseline: the exact layers this patch's deltas apply to.
  std::vector<PakCatalogBase> bases;
  //! Libraries whose assets and textures this pak references without
  //! containing them, such as `Engine`.
  std::vector<PakCatalogDependency> dependencies;

  //! Canonical digest of the library, physical entries, deletions, ordered
  //! base identities and dependencies.
  OXGN_DATA_NDAPI auto ComputeDigest() const
    -> Result<base::Sha256Digest, std::string>;
  OXGN_DATA_NDAPI auto Validate() const -> Result<void, std::string>;
  //! Required bases must be the contiguous suffix below this layer.
  OXGN_DATA_NDAPI auto ValidateBaseLayers(
    std::span<const PakCatalogBase> lower_layers) const
    -> Result<void, std::string>;
  OXGN_DATA_NDAPI auto Encode() const
    -> Result<std::vector<std::byte>, std::string>;
  //! Decodes an encoded catalog; a version this build cannot read is
  //! `CookedOpenFailure::kUnsupportedVersion`, anything malformed
  //! `kCorrupt`.
  OXGN_DATA_NDAPI static auto Decode(std::span<const std::byte> bytes)
    -> Result<PakCatalog, CookedOpenError>;
};

} // namespace oxygen::data
