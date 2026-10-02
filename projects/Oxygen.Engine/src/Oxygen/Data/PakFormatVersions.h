//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <Oxygen/Data/AssetType.h>

namespace oxygen::data::pak::version {
#define OXDAT_FORMAT_VERSION(name, type, value)                                \
  inline constexpr std::type name = value;
#define OXDAT_ASSET_VERSION(name, asset_type, spec_name, value)                \
  inline constexpr std::uint8_t name = value;
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
} // namespace oxygen::data::pak::version

namespace oxygen::data::pak {
[[nodiscard]] constexpr auto CurrentAssetVersion(const AssetType type) noexcept
  -> std::optional<std::uint8_t>
{
  switch (type) {
#define OXDAT_FORMAT_VERSION(name, underlying_type, value)
#define OXDAT_ASSET_VERSION(name, asset_type, spec_name, value)                \
  case AssetType::asset_type:                                                  \
    return version::name;
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
  case AssetType::kUnknown:
    return std::nullopt;
  }
  return std::nullopt;
}
} // namespace oxygen::data::pak
