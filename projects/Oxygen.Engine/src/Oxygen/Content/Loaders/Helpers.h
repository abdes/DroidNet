//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::loaders {

//! Loads an AssetHeader, using the provided \b reader.
/*!

 @tparam S Stream type
 @param reader The reader to use for loading
 @return The loaded AssetHeader

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: No heap allocation
 - Optimization: Reads fields individually to avoid padding/representation
 issues

 @see oxygen::data::pak::AssetHeader
*/
template <oxygen::serio::Stream S>
auto LoadAssetHeader(oxygen::serio::Reader<S>& reader)
  -> oxygen::data::pak::AssetHeader
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::AssetHeader;

  LOG_SCOPE_F(INFO, "Header");

  auto result = reader.template read<AssetHeader>();
  if (!result) {
    LOG_F(INFO, "-failed- on AssetHeader: {}", result.error().message());
    throw std::runtime_error(
      fmt::format("error reading asset header: {}", result.error().message()));
  }

  // Check validity of the header
  auto asset_type = result.value().asset_type;
  if (asset_type > static_cast<uint8_t>(AssetType::kMaxAssetType)) {
    LOG_F(INFO, "-failed- on invalid asset type {}", asset_type);
    throw std::runtime_error(
      fmt::format("invalid asset type in header: {}", asset_type));
  }
  LOG_F(INFO, "asset type         : {}",
    nostd::to_string(static_cast<AssetType>(asset_type)));

  // Check that name contains a null terminator
  const auto& name = result.value().name;
  if (std::find(std::begin(name), std::end(name), '\0') == std::end(name)) {
    // We let it go, because the name getter will handle this case and return a
    // valid std::string_view. But we log a warning to help debugging.
    LOG_F(WARNING, "-fishy- on name not null-terminated");
  }
  std::string_view name_view(name, oxygen::data::pak::kMaxNameSize);
  LOG_F(INFO, "asset name         : {}", name_view);

  LOG_F(INFO, "format version     : {}", result.value().version);
  LOG_F(INFO, "variant flags      : 0x{:08X}", result.value().variant_flags);
  LOG_F(INFO, "streaming priority : {}", result.value().streaming_priority);
  LOG_F(INFO, "content hash       : 0x{:016X}", result.value().content_hash);

  return result.value();
}

} // namespace oxygen::content::loaders
