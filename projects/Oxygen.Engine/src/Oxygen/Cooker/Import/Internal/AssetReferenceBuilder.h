//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import {

//! Per-descriptor reference interning while a native producer serializes
//! fields.
class AssetReferenceBuilder final {
public:
  AssetReferenceBuilder() = default;
  OXGN_COOK_API explicit AssetReferenceBuilder(
    std::span<const data::ResourceBinding> resources);
  OXGN_COOK_API explicit AssetReferenceBuilder(
    const data::AssetReferences& references);
  OXGN_COOK_NDAPI auto AddResource(data::ResourceKind kind,
    ResourceIndexT index) -> data::ResourceReferenceIndex;
  OXGN_COOK_API auto AddAsset(const data::AssetKey& key, data::AssetType type)
    -> void;
  OXGN_COOK_API auto AddPhysicsResource(const data::AssetKey& key) -> void;
  OXGN_COOK_API auto AddLogical(const data::AssetKey& key,
    data::AssetType type = data::AssetType::kUnknown) -> void;
  OXGN_COOK_NDAPI auto Build() && -> data::AssetReferences;
  OXGN_COOK_API auto AddKey(data::KeyReferenceKind kind,
    const data::AssetKey& key, data::AssetType type) -> void;

private:
  std::vector<data::ResourceBinding> resources_;
  std::unordered_map<uint64_t, data::ResourceReferenceIndex> resource_indices_;
  std::map<std::pair<data::KeyReferenceKind, data::AssetKey>, data::AssetType>
    keys_;
};

} // namespace oxygen::content::import
