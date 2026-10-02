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
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

struct PakCatalogEntry final {
  AssetKey asset_key {};
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

struct PakCatalog final {
  SourceKey source_key {};
  uint16_t content_version = 0;
  std::array<uint8_t, 32> catalog_digest {};
  std::vector<PakCatalogEntry> entries;
  std::vector<AssetKey> deleted {};
  std::vector<PakCatalogBase> bases {};

  //! Canonical digest of physical entries, deletions and ordered base
  //! identities.
  OXGN_DATA_NDAPI auto ComputeDigest() const
    -> Result<base::Sha256Digest, std::string>;
  OXGN_DATA_NDAPI auto Validate() const -> Result<void, std::string>;
  //! Required bases must be the contiguous suffix below this layer.
  OXGN_DATA_NDAPI auto ValidateBaseLayers(
    std::span<const PakCatalogBase> lower_layers) const
    -> Result<void, std::string>;
  OXGN_DATA_NDAPI auto Encode() const
    -> Result<std::vector<std::byte>, std::string>;
  OXGN_DATA_NDAPI static auto Decode(std::span<const std::byte> bytes)
    -> Result<PakCatalog, std::string>;
};

} // namespace oxygen::data
