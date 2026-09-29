//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include <Oxygen/Content/Internal/DependencyGraphStore.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/RefCountedEviction.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::internal {

class DependencyReleaseEngine final {
public:
  using CacheT = AnyCache<uint64_t, RefCountedEviction<uint64_t>>;

  struct TrimResult final {
    size_t trim_roots = 0;
    size_t pruned_live_branches = 0;
    size_t blocked_priority_roots = 0;
    size_t orphan_resources = 0;
  };

  auto ReleaseAssetTree(
    uint64_t key, DependencyGraphStore& graph, CacheT& content_cache) -> void;

  auto TrimCache(std::span<const uint64_t> asset_keys,
    std::span<const uint64_t> resource_keys, DependencyGraphStore& graph,
    CacheT& content_cache) -> TrimResult;
};

} // namespace oxygen::content::internal
