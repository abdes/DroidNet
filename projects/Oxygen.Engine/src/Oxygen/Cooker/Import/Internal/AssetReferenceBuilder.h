//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import {

//! Per-descriptor reference interning while a native producer serializes
//! fields.
class AssetReferenceBuilder final {
public:
  AssetReferenceBuilder() = default;
  //! Start from local resource bindings; an external texture binding needs
  //! its key, so it is refused here.
  OXGN_COOK_API explicit AssetReferenceBuilder(
    std::span<const data::ResourceBinding> resources);
  //! Start from complete references, keeping the key references `keep`
  //! accepts; the texture keys external bindings name are always kept.
  OXGN_COOK_API explicit AssetReferenceBuilder(
    const data::AssetReferences& references,
    const std::function<bool(const data::KeyReference&)>& keep = {});
  OXGN_COOK_NDAPI auto AddResource(data::ResourceKind kind,
    ResourceIndexT index) -> data::ResourceReferenceIndex;
  OXGN_COOK_API auto AddAsset(const data::AssetKey& key, data::AssetType type)
    -> void;
  OXGN_COOK_API auto AddPhysicsResource(const data::AssetKey& key) -> void;
  //! Bind a texture another source holds, by its texture key; one binding per
  //! texture, however many fields use it.
  OXGN_COOK_NDAPI auto AddExternalTexture(const data::AssetKey& texture_key)
    -> data::ResourceReferenceIndex;
  //! Bind a resolved texture: a local texture-table entry or another source's
  //! texture by key. Nothing, and the absent entry, bind no texture.
  OXGN_COOK_NDAPI auto AddTexture(const data::TextureTarget& target)
    -> data::ResourceReferenceIndex;
  OXGN_COOK_API auto AddLogical(const data::AssetKey& key,
    data::AssetType type = data::AssetType::kUnknown) -> void;
  OXGN_COOK_NDAPI auto Build() && -> data::AssetReferences;
  OXGN_COOK_API auto AddKey(data::KeyReferenceKind kind,
    const data::AssetKey& key, data::AssetType type) -> void;

private:
  std::vector<data::ResourceBinding> resources_;
  std::unordered_map<uint64_t, data::ResourceReferenceIndex> resource_indices_;
  // External texture bindings by texture key; their key-table positions are
  // known only when Build orders the keys.
  std::map<data::AssetKey, data::ResourceReferenceIndex> external_textures_;
  std::map<std::pair<data::KeyReferenceKind, data::AssetKey>, data::AssetType>
    keys_;
};

} // namespace oxygen::content::import
