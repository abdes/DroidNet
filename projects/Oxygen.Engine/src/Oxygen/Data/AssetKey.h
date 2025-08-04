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

//! Uniquely identifies an asset in the system, using a 128-bit GUID.
struct AssetKey {
  std::array<uint8_t, 16> guid; //!< 128-bit GUID (raw bytes)

  auto operator<=>(const AssetKey&) const = default;
};
static_assert(sizeof(AssetKey) == 16);

//! String representation of AssetKey.
OXGN_DATA_NDAPI auto to_string(AssetKey value) noexcept -> std::string;

//! Generates a random 128-bit GUID using stduuid and stores as array of bytes.
OXGN_DATA_NDAPI auto GenerateAssetGuid() -> std::array<uint8_t, 16>;

} // namespace oxygen::data

//! Hash specialization for AssetKey.
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
