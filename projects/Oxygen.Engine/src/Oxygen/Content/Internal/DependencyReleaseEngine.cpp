//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

#ifndef NDEBUG
#  include <Oxygen/Base/Logging.h>
#endif
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Content/Internal/DependencyGraphStore.h>
#include <Oxygen/Content/Internal/DependencyReleaseEngine.h>

namespace oxygen::content::internal {

auto DependencyReleaseEngine::ReleaseAssetTree(const uint64_t key,
  DependencyGraphStore& graph, CacheT& content_cache) -> void
{
#ifndef NDEBUG
  // Cycles are rejected upstream; the recursion guard is debug diagnostics.
  static thread_local std::unordered_set<uint64_t> release_visit_set;
  const bool inserted = release_visit_set.emplace(key).second;
  DCHECK_F(inserted, "Cycle encountered during ReleaseAssetTree recursion");
  const auto visit_guard
    = ScopeGuard([key] noexcept -> void { release_visit_set.erase(key); });
#endif

  // Detach before callbacks can re-enter the loader and mutate its graph.
  auto resource_deps = graph.RemoveResourceDependencies(key);
  auto asset_deps = graph.RemoveAssetDependencies(key);
  if (resource_deps) {
    for (const auto resource : *resource_deps) {
      content_cache.CheckIn(resource.get());
    }
  }
  if (asset_deps) {
    for (const auto dependency : *asset_deps) {
      if (content_cache.GetCheckoutCount(dependency) > 1U) {
        content_cache.CheckIn(dependency);
      } else {
        ReleaseAssetTree(dependency, graph, content_cache);
      }
    }
  }
  content_cache.CheckIn(key);
}

auto DependencyReleaseEngine::TrimCache(std::span<const uint64_t> asset_keys,
  std::span<const uint64_t> resource_keys, DependencyGraphStore& graph,
  CacheT& content_cache) -> TrimResult
{
  std::vector<uint64_t> trim_roots;
  trim_roots.reserve(asset_keys.size());
  size_t blocked_roots = 0U;
  for (const auto cache_key : asset_keys) {
    const auto checkout_count = content_cache.GetCheckoutCount(cache_key);
    if (checkout_count == 1U) {
      trim_roots.push_back(cache_key);
    } else if (checkout_count > 1U) {
      ++blocked_roots;
    }
  }
  std::ranges::sort(trim_roots);

  std::unordered_set<uint64_t> visited_assets;
  std::unordered_set<uint64_t> visiting_assets;
  visited_assets.reserve(asset_keys.size());
  visiting_assets.reserve(asset_keys.size());
  size_t pruned_live_branches = 0U;

  auto trim_asset = [&](auto&& self, const uint64_t asset_hash) -> void {
    if (visited_assets.contains(asset_hash)
      || !content_cache.Contains(asset_hash)) {
      return;
    }
    if (content_cache.GetCheckoutCount(asset_hash) > 1U) {
      // Another parent can release its edge later in this pass. Do not mark
      // this node visited until its own outgoing edges have been released.
      ++pruned_live_branches;
      return;
    }
    if (!visiting_assets.insert(asset_hash).second) {
      return;
    }
    const auto visiting_guard
      = ScopeGuard([&visiting_assets, asset_hash] noexcept -> void {
          visiting_assets.erase(asset_hash);
        });

    auto resource_deps = graph.RemoveResourceDependencies(asset_hash);
    auto asset_deps = graph.RemoveAssetDependencies(asset_hash);
    if (resource_deps) {
      for (const auto resource : *resource_deps) {
        const auto resource_hash = resource.get();
        content_cache.CheckIn(resource_hash);
        if (content_cache.Contains(resource_hash)
          && content_cache.GetCheckoutCount(resource_hash) == 1U) {
          static_cast<void>(content_cache.Remove(resource_hash));
        }
      }
    }
    if (asset_deps) {
      for (const auto dependency : *asset_deps) {
        content_cache.CheckIn(dependency);
        self(self, dependency);
      }
    }

    content_cache.CheckIn(asset_hash);
    visited_assets.insert(asset_hash);
  };

  for (const auto root : trim_roots) {
    trim_asset(trim_asset, root);
  }

  std::vector<uint64_t> resource_hashes;
  resource_hashes.reserve(resource_keys.size());
  for (const auto cache_key : resource_keys) {
    resource_hashes.push_back(cache_key);
  }
  std::ranges::sort(resource_hashes);

  size_t orphan_resources = 0U;
  for (const auto resource_hash : resource_hashes) {
    if (content_cache.Contains(resource_hash)
      && content_cache.GetCheckoutCount(resource_hash) == 1U) {
      ++orphan_resources;
      static_cast<void>(content_cache.Remove(resource_hash));
    }
  }
  return TrimResult {
    .trim_roots = trim_roots.size(),
    .pruned_live_branches = pruned_live_branches,
    .blocked_priority_roots = blocked_roots,
    .orphan_resources = orphan_resources,
  };
}

} // namespace oxygen::content::internal
