//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/DependencyReleaseEngine.h>

namespace oxygen::content::internal {

auto DependencyReleaseEngine::ReleaseAssetTree(const data::AssetKey& key,
  DependencyGraphStore& graph, CacheT& content_cache,
  const ReleaseCallbacks& callbacks) -> void
{
#if !defined(NDEBUG)
  // Policy: release-recursion visit guard is debug diagnostics only.
  // Release assumes acyclic graphs validated upstream.
  static thread_local std::unordered_set<data::AssetKey> release_visit_set;
  const bool inserted = release_visit_set.emplace(key).second;
  DCHECK_F(inserted, "Cycle encountered during ReleaseAssetTree recursion");
  class VisitGuard final {
  public:
    VisitGuard(
      std::unordered_set<data::AssetKey>& set, data::AssetKey k) noexcept
      : set_(set)
      , key_(std::move(k))
    {
    }
    ~VisitGuard() { set_.erase(key_); }
    VisitGuard(const VisitGuard&) = delete;
    VisitGuard& operator=(const VisitGuard&) = delete;

  private:
    std::unordered_set<data::AssetKey>& set_;
    data::AssetKey key_;
  } visit_guard(release_visit_set, key);
#endif

  // Release resource dependencies first.
  if (const auto* resource_deps = graph.FindResourceDependencies(key);
    resource_deps != nullptr) {
    for (const auto& res_key : *resource_deps) {
      content_cache.CheckIn(callbacks.hash_resource(res_key));
    }
    (void)graph.RemoveResourceDependencies(key);
  }

  // Then release asset dependencies.
  if (const auto* asset_deps = graph.FindAssetDependencies(key);
    asset_deps != nullptr) {
    for (const auto& dep_key : *asset_deps) {
      const auto dep_hash_opt = callbacks.resolve_asset_hash(dep_key);
      const auto resolved_hash = dep_hash_opt.has_value()
        ? *dep_hash_opt
        : callbacks.hash_asset_fallback(dep_key);
      const auto dep_checkout_count
        = content_cache.GetCheckoutCount(resolved_hash);

      // Only recurse when retained solely by current dependency edge.
      if (dep_checkout_count > 1U) {
        content_cache.CheckIn(resolved_hash);
        continue;
      }

      ReleaseAssetTree(dep_key, graph, content_cache, callbacks);
    }
    (void)graph.RemoveAssetDependencies(key);
  }

  // Release the asset itself.
  const auto key_hash_opt = callbacks.resolve_asset_hash(key);
  content_cache.CheckIn(key_hash_opt.has_value()
      ? *key_hash_opt
      : callbacks.hash_asset_fallback(key));
  callbacks.assert_refcount_symmetry("ReleaseAssetTree");
}

auto DependencyReleaseEngine::TrimCache(
  const std::unordered_map<uint64_t, data::AssetKey>& asset_keys,
  const std::unordered_map<uint64_t, ResourceKey>& resource_keys,
  DependencyGraphStore& graph, CacheT& content_cache,
  const ReleaseCallbacks& callbacks) -> TrimResult
{
  std::unordered_map<data::AssetKey, uint64_t> hash_by_asset_key;
  hash_by_asset_key.reserve(asset_keys.size());
  for (const auto& [hash_key, asset_key] : asset_keys) {
    if (content_cache.Contains(hash_key)) {
      hash_by_asset_key.insert_or_assign(asset_key, hash_key);
    }
  }

  std::vector<data::AssetKey> trim_roots;
  trim_roots.reserve(hash_by_asset_key.size());
  for (const auto& [asset_key, hash_key] : hash_by_asset_key) {
    if (!content_cache.Contains(hash_key)) {
      continue;
    }
    if (content_cache.GetCheckoutCount(hash_key) == 1U) {
      trim_roots.push_back(asset_key);
    }
  }

  std::unordered_set<data::AssetKey> visited_assets;
  std::unordered_set<data::AssetKey> visiting_assets;
  size_t pruned_live_branches = 0U;

  std::function<void(const data::AssetKey&)> trim_asset_dfs;
  trim_asset_dfs = [&](const data::AssetKey& asset_key) {
    if (visited_assets.contains(asset_key)) {
      return;
    }
    if (!visiting_assets.insert(asset_key).second) {
      return;
    }

    struct VisitingGuard final {
      std::unordered_set<data::AssetKey>& visiting;
      std::unordered_set<data::AssetKey>& visited;
      data::AssetKey key;
      ~VisitingGuard() noexcept
      {
        visiting.erase(key);
        visited.insert(key);
      }
    } visiting_guard { visiting_assets, visited_assets, asset_key };

    const auto hash_it = hash_by_asset_key.find(asset_key);
    if (hash_it == hash_by_asset_key.end()) {
      return;
    }
    const auto asset_hash = hash_it->second;
    if (!content_cache.Contains(asset_hash)) {
      return;
    }

    if (content_cache.GetCheckoutCount(asset_hash) > 1U) {
      ++pruned_live_branches;
      return;
    }

    if (const auto* asset_deps = graph.FindAssetDependencies(asset_key);
      asset_deps != nullptr) {
      for (const auto& dep_asset_key : *asset_deps) {
        trim_asset_dfs(dep_asset_key);
      }
    }

    if (const auto* resource_deps = graph.FindResourceDependencies(asset_key);
      resource_deps != nullptr) {
      for (const auto& resource_key : *resource_deps) {
        const auto resource_hash = callbacks.hash_resource(resource_key);
        if (!content_cache.Contains(resource_hash)) {
          continue;
        }
        content_cache.CheckIn(resource_hash);
        if (content_cache.Contains(resource_hash)
          && content_cache.GetCheckoutCount(resource_hash) == 1U) {
          (void)content_cache.Remove(resource_hash);
        }
      }
      (void)graph.RemoveResourceDependencies(asset_key);
    }

    if (const auto* asset_deps = graph.FindAssetDependencies(asset_key);
      asset_deps != nullptr) {
      for (const auto& dep_asset_key : *asset_deps) {
        const auto dep_hash_it = hash_by_asset_key.find(dep_asset_key);
        if (dep_hash_it == hash_by_asset_key.end()) {
          continue;
        }
        const auto dep_hash = dep_hash_it->second;
        if (!content_cache.Contains(dep_hash)) {
          continue;
        }
        content_cache.CheckIn(dep_hash);
        if (content_cache.Contains(dep_hash)
          && content_cache.GetCheckoutCount(dep_hash) == 1U) {
          trim_asset_dfs(dep_asset_key);
          if (content_cache.Contains(dep_hash)
            && content_cache.GetCheckoutCount(dep_hash) == 1U) {
            (void)content_cache.Remove(dep_hash);
          }
        }
      }
      (void)graph.RemoveAssetDependencies(asset_key);
    }

    content_cache.CheckIn(asset_hash);
    if (content_cache.Contains(asset_hash)
      && content_cache.GetCheckoutCount(asset_hash) == 1U) {
      (void)content_cache.Remove(asset_hash);
    }
  };

  for (const auto& root_asset_key : trim_roots) {
    trim_asset_dfs(root_asset_key);
  }

  std::vector<uint64_t> resource_hash_snapshot;
  resource_hash_snapshot.reserve(resource_keys.size());
  for (const auto& [hash_key, resource_key] : resource_keys) {
    static_cast<void>(resource_key);
    resource_hash_snapshot.push_back(hash_key);
  }

  size_t standalone_resource_candidates = 0U;
  for (const auto resource_hash : resource_hash_snapshot) {
    if (!content_cache.Contains(resource_hash)) {
      continue;
    }
    if (content_cache.GetCheckoutCount(resource_hash) == 1U) {
      ++standalone_resource_candidates;
      (void)content_cache.Remove(resource_hash);
    }
  }

  return TrimResult {
    .trim_roots = trim_roots.size(),
    .pruned_live_branches = pruned_live_branches,
    .standalone_resource_candidates = standalone_resource_candidates,
  };
}

} // namespace oxygen::content::internal
