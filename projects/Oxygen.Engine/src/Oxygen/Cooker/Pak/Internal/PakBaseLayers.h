//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_map>
#include <vector>

#include <Oxygen/Cooker/Pak/PakTextures.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>

namespace oxygen::content::pak::internal {

//! What a patch applies to: the assets and textures of its base layers, each
//! layer applied above the ones below it, its deletions included.
/*!
 The planner and the validator compose the bases with it, so a key a base
 deleted and a later base recreated exists for both, and a key a base deleted
 exists for neither.
*/
struct EffectiveBase {
  //! The assets that exist after every layer.
  std::unordered_map<data::AssetKey, data::PakCatalogEntry> entries;
  //! The keyed textures that exist after every layer.
  PakTextures textures;
  //! Every asset type a layer declares, deleted assets included: a key keeps
  //! its type through every layer.
  std::unordered_map<data::AssetKey, data::AssetType> asset_types;

  //! Applies `layer`, whose pak holds `layer_textures`, above the layers
  //! applied so far; returns the keys it gives another type than a lower layer
  //! did, which it leaves out.
  auto Apply(const data::PakCatalog& layer, const PakTextures& layer_textures)
    -> std::vector<data::AssetKey>
  {
    auto mismatches = std::vector<data::AssetKey> {};
    for (const auto& entry : layer.entries) {
      const auto [known, inserted]
        = asset_types.emplace(entry.asset_key, entry.asset_type);
      if (!inserted && known->second != entry.asset_type) {
        mismatches.push_back(entry.asset_key);
        continue;
      }
      entries.insert_or_assign(entry.asset_key, entry);
    }
    for (const auto& key : layer.deleted) {
      entries.erase(key);
      textures.erase(key);
    }
    for (const auto& [key, digest] : layer_textures) {
      textures.insert_or_assign(key, digest);
    }
    return mismatches;
  }
};

} // namespace oxygen::content::pak::internal
