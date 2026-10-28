//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <Oxygen/Data/AssetType.h>

// The version table is an X-macro list shared with the tools; the macros below
// are the only way to expand it, and the declaration-position parameters cannot
// be parenthesized.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
namespace oxygen::data::pak::version {
#define OXDAT_FORMAT_VERSION(name, spec_name, type, value)                     \
  inline constexpr std::type name = value;
#define OXDAT_ASSET_VERSION(name, asset_type, spec_name, value)                \
  inline constexpr std::uint8_t name = value;
#define OXDAT_IMPORTER_REVISION(importer, spec_name, value)
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_IMPORTER_REVISION
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION

//! A cooked format or asset, by the name tools report it with, and its current
//! version.
struct NamedVersion final {
  std::string_view name;
  std::uint32_t value = 0;
};

//! The current version of every cooked format that is not an asset.
inline constexpr auto kFormats = std::to_array<NamedVersion>({
#define OXDAT_FORMAT_VERSION(id, spec_name, type, number)                      \
  { .name = #spec_name, .value = (id) },
#define OXDAT_ASSET_VERSION(id, asset_type, spec_name, number)
#define OXDAT_IMPORTER_REVISION(importer, spec_name, number)
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_IMPORTER_REVISION
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
});

//! The current version of every cooked asset type.
inline constexpr auto kAssets = std::to_array<NamedVersion>({
#define OXDAT_FORMAT_VERSION(id, spec_name, type, number)
#define OXDAT_ASSET_VERSION(id, asset_type, spec_name, number)                 \
  { .name = #spec_name, .value = (id) },
#define OXDAT_IMPORTER_REVISION(importer, spec_name, number)
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_IMPORTER_REVISION
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
});
} // namespace oxygen::data::pak::version

namespace oxygen::data::pak {
[[nodiscard]] constexpr auto CurrentAssetVersion(const AssetType type) noexcept
  -> std::optional<std::uint8_t>
{
  switch (type) {
#define OXDAT_FORMAT_VERSION(name, spec_name, underlying_type, value)
#define OXDAT_ASSET_VERSION(name, asset_type, spec_name, value)                \
  case AssetType::asset_type:                                                  \
    return version::name;
#define OXDAT_IMPORTER_REVISION(importer, spec_name, value)
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_IMPORTER_REVISION
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
  case AssetType::kUnknown:
    return std::nullopt;
  }
  return std::nullopt;
}
} // namespace oxygen::data::pak
// NOLINTEND(cppcoreguidelines-macro-usage)
