//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Content/Internal/DependencyGraphStore.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/RefCountedEviction.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::internal {

class DependencyReleaseEngine final {
public:
  using CacheT = AnyCache<uint64_t, RefCountedEviction<uint64_t>>;

  struct ReleaseCallbacks final {
    std::function<std::optional<uint64_t>(const data::AssetKey&)>
      resolve_asset_hash;
    std::function<uint64_t(const data::AssetKey&)> hash_asset_fallback;
    std::function<uint64_t(ResourceKey)> hash_resource;
    std::function<void(std::string_view)> assert_refcount_symmetry;
  };

  struct TrimResult final {
    size_t trim_roots = 0;
    size_t pruned_live_branches = 0;
    size_t blocked_priority_roots = 0;
    size_t orphan_resources = 0;
  };

  auto ReleaseAssetTree(const data::AssetKey& key, DependencyGraphStore& graph,
    CacheT& content_cache, const ReleaseCallbacks& callbacks) -> void;

  auto TrimCache(const std::unordered_map<uint64_t, data::AssetKey>& asset_keys,
    const std::unordered_map<uint64_t, ResourceKey>& resource_keys,
    DependencyGraphStore& graph, CacheT& content_cache,
    const ReleaseCallbacks& callbacks) -> TrimResult;
};

} // namespace oxygen::content::internal
