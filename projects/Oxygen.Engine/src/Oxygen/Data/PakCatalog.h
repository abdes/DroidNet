//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::data {

struct PakCatalogEntry final {
  AssetKey asset_key {};
  AssetType asset_type = AssetType::kUnknown;
  std::array<uint8_t, 32> descriptor_digest {};
  std::array<uint8_t, 32> transitive_resource_digest {};
};

struct PakCatalog final {
  SourceKey source_key {};
  uint16_t content_version = 0;
  std::array<uint8_t, 32> catalog_digest {};
  std::vector<PakCatalogEntry> entries;
};

} // namespace oxygen::data
