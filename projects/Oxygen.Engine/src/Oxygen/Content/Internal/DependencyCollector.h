//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_set>
#include <vector>

#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::internal {

//! Collects dependency identities during async decode steps.
/*!
 Holds only identity types (AssetKey, ResourceKey, ResourceRef). It MUST NOT
 store locators, paths, streams, readers, or other access state.

 This is used as a Decode -> Publish handoff. Publish code is responsible for
 binding ResourceRef to ResourceKey and mutating the dependency graph.
*/
struct DependencyCollector final {
  auto AddAssetDependency(const data::AssetKey& key) -> void
  {
    if (asset_seen_.insert(key).second) {
      asset_dependencies_.push_back(key);
    }
  }

  auto AddResourceDependency(ResourceKey key) -> void
  {
    if (resource_key_seen_.insert(key).second) {
      resource_key_dependencies_.push_back(key);
    }
  }

  auto AddResourceDependency(const ResourceRef& ref) -> void
  {
    if (resource_ref_seen_.insert(ref).second) {
      resource_ref_dependencies_.push_back(ref);
    }
  }

  [[nodiscard]] auto AssetDependencies() const
    -> const std::vector<data::AssetKey>&
  {
    return asset_dependencies_;
  }

  [[nodiscard]] auto ResourceKeyDependencies() const
    -> const std::vector<ResourceKey>&
  {
    return resource_key_dependencies_;
  }

  [[nodiscard]] auto ResourceRefDependencies() const
    -> const std::vector<ResourceRef>&
  {
    return resource_ref_dependencies_;
  }

private:
  std::unordered_set<data::AssetKey> asset_seen_;
  std::vector<data::AssetKey> asset_dependencies_;

  std::unordered_set<ResourceKey> resource_key_seen_;
  std::vector<ResourceKey> resource_key_dependencies_;

  std::unordered_set<ResourceRef> resource_ref_seen_;
  std::vector<ResourceRef> resource_ref_dependencies_;
};

} // namespace oxygen::content::internal
