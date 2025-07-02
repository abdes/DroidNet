//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <compare>
#include <cstdint>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Uniquely identifies an asset in the system
/*!
  The 'guid' field is a 128-bit value stored as std::array<uint8_t, 16>.
  The 'variant' field is a 32-bit project-defined mask or flag value. The engine
  does not interpret this field; it is available for project-specific use (e.g.,
  localization, skin, region, quality, animation set, etc.). LODs are always
  built-in to geometry assets and are NOT represented by variant.

  Geometry asset structure:
    - Geometry: one or more LODs (indexed 0..N-1)
    - Each LOD: a Mesh asset
    - Each Mesh: zero or more MeshViews (sub-meshes), `no view` -> entire mesh
*/
struct AssetKey {
  std::array<uint8_t, 16> guid; //!< 128-bit GUID (raw bytes)

  auto operator<=>(const AssetKey&) const = default;
};
static_assert(sizeof(AssetKey) == 16);

//! String representation of enum values in `Format`.
OXGN_DATA_NDAPI auto to_string(AssetKey value) noexcept -> std::string;

//! Generates a random 128-bit GUID using stduuid and stores as array of bytes.
OXGN_DATA_NDAPI auto GenerateAssetGuid() -> std::array<uint8_t, 16>;

} // namespace oxygen::data

template <> struct std::hash<oxygen::data::AssetKey> {
  size_t operator()(const oxygen::data::AssetKey& key) const noexcept
  {
    size_t seed = 0;
    for (auto b : key.guid) {
      oxygen::HashCombine(seed, b);
    }
    return seed;
  }
};
