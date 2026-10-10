//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::testing {

//! Asset key whose first byte is `seed` and whose other bytes are zero.
[[nodiscard]] inline auto MakeFirstByteAssetKey(const uint8_t seed)
  -> oxygen::data::AssetKey
{
  auto bytes = std::array<uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  bytes.at(0) = seed;
  return oxygen::data::AssetKey::FromBytes(bytes);
}

} // namespace oxygen::content::testing
