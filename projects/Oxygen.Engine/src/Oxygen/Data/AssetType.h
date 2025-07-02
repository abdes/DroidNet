//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Asset types
enum class AssetType : uint8_t {
  // clang-format off
  kUnknown      = 0,

  kMaterial     = 1,
  kGeometry     = 2,
  kScene        = 3,

  //!< Maximum value sentinel
  kMaxAssetType = kScene
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<AssetType>) <= sizeof(uint8_t),
  "AssetType enum fit in `uint8_t` for compatibility with PAK format");

//! String representation of enum values in `AssetType`.
OXGN_DATA_NDAPI auto to_string(AssetType value) noexcept -> const char*;

} // namespace oxygen::data
