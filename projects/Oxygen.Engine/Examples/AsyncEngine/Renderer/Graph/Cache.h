//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Scheduler.h"
#include "Types.h"

namespace oxygen::examples::asyncsim {

// Forward declarations
class RenderGraph;

//! Cache key for render graph lookups
/*!
 Uniquely identifies a render graph configuration for caching purposes.
 Uses deterministic hashing to ensure consistent cache hits.
 */
struct RenderGraphCacheKey {
  uint64_t structure_hash {
    0
  }; //!< Hash of graph structure (passes, dependencies)
  uint64_t resource_hash { 0 }; //!< Hash of resource configurations
  uint64_t viewport_hash { 0 }; //!< Hash of viewport configurations
  uint32_t view_count { 0 }; //!< Number of views

  RenderGraphCacheKey() = default;

  //! Calculate combined hash for cache lookup
  [[nodiscard]] auto GetCombinedHash() const -> uint64_t
  {
    // Simple combination - in real implementation would use better hash
    // combining
    return structure_hash ^ (resource_hash << 1) ^ (viewport_hash << 2)
      ^ view_count;
  }

  //! Equality operator for cache lookups
  auto operator==(const RenderGraphCacheKey& other) const -> bool
  {
    return structure_hash == other.structure_hash
      && resource_hash == other.resource_hash
      && viewport_hash == other.viewport_hash && view_count == other.view_count;
  }
};

//! Cache entry for compiled render graphs
struct RenderGraphCacheEntry {
  std::shared_ptr<RenderGraph> compiled_graph; //!< Compiled render graph
  SchedulingResult scheduling_result; //!< Cached scheduling result
  uint64_t creation_time { 0 }; //!< Creation timestamp
  uint32_t access_count { 0 }; //!< Number of times accessed
  size_t memory_usage { 0 }; //!< Estimated memory usage

  RenderGraphCacheEntry() = default;

  explicit RenderGraphCacheEntry(std::shared_ptr<RenderGraph> graph)
    : compiled_graph(std::move(graph))
    , creation_time(0) // Would use actual timestamp in real implementation
  {
  }
};

//! Interface for render graph caching
/*!
 Provides caching of compiled render graphs to avoid recompilation when
 the graph structure hasn't changed. Uses LRU eviction and memory bounds.
 */
class RenderGraphCache {
public:
  RenderGraphCache() = default;
  virtual ~RenderGraphCache() = default;

  // Non-copyable, movable
  RenderGraphCache(const RenderGraphCache&) = delete;
  auto operator=(const RenderGraphCache&) -> RenderGraphCache& = delete;
  RenderGraphCache(RenderGraphCache&&) = default;
  auto operator=(RenderGraphCache&&) -> RenderGraphCache& = default;

  //! Get cached render graph by key
  [[nodiscard]] virtual auto Get(const RenderGraphCacheKey& key)
    -> std::shared_ptr<RenderGraph>
  {
    // Stub implementation - Phase 1
    auto it = cache_entries_.find(key.GetCombinedHash());
    if (it != cache_entries_.end()) {
      it->second.access_count++;
      return it->second.compiled_graph;
    }
    return nullptr;
  }

  //! Store render graph in cache
  virtual auto Set(const RenderGraphCacheKey& key,
    std::shared_ptr<RenderGraph> graph, const SchedulingResult& scheduling)
    -> void
  {
    auto entry = RenderGraphCacheEntry(std::move(graph));
    entry.scheduling_result = scheduling;
    cache_entries_[key.GetCombinedHash()] = std::move(entry);

    // Simple memory management - would implement proper LRU in real version
    if (cache_entries_.size() > max_cache_entries_) {
      // Remove oldest entry (simple implementation)
      auto oldest = cache_entries_.begin();
      cache_entries_.erase(oldest);
    }
  }

  //! Check if key exists in cache
  [[nodiscard]] virtual auto Contains(const RenderGraphCacheKey& key) const
    -> bool
  {
    return cache_entries_.find(key.GetCombinedHash()) != cache_entries_.end();
  }

  //! Invalidate cache entry
  virtual auto Invalidate(const RenderGraphCacheKey& key) -> void
  {
    cache_entries_.erase(key.GetCombinedHash());
  }

  //! Clear all cache entries
  virtual auto Clear() -> void { cache_entries_.clear(); }

  //! Set maximum cache size
  virtual auto SetMaxCacheEntries(size_t max_entries) -> void
  {
    max_cache_entries_ = max_entries;
  }

  //! Set maximum memory usage
  virtual auto SetMaxMemoryUsage(size_t max_bytes) -> void
  {
    max_memory_bytes_ = max_bytes;
  }

  //! Get cache statistics
  [[nodiscard]] virtual auto GetCacheStats() const -> std::string
  {
    return "Cache entries: " + std::to_string(cache_entries_.size()) + "/"
      + std::to_string(max_cache_entries_);
  }

  //! Get memory usage estimate
  [[nodiscard]] virtual auto GetMemoryUsage() const -> size_t
  {
    size_t total = 0;
    for (const auto& [key, entry] : cache_entries_) {
      total += entry.memory_usage;
    }
    return total;
  }

protected:
  std::unordered_map<uint64_t, RenderGraphCacheEntry> cache_entries_;
  size_t max_cache_entries_ { 32 };
  size_t max_memory_bytes_ { 64 * 1024 * 1024 }; // 64MB default
};

//! Interface for compilation result caching
/*!
 Caches intermediate compilation results like dependency graphs,
 resource lifetime analysis, and validation results.
 */
class CompilationCache {
public:
  CompilationCache() = default;
  virtual ~CompilationCache() = default;

  // Non-copyable, movable
  CompilationCache(const CompilationCache&) = delete;
  auto operator=(const CompilationCache&) -> CompilationCache& = delete;
  CompilationCache(CompilationCache&&) = default;
  auto operator=(CompilationCache&&) -> CompilationCache& = default;

  //! Cache dependency graph
  virtual auto CacheDependencyGraph(const RenderGraphCacheKey& key,
    const std::vector<PassHandle>& execution_order) -> void
  {
    // Stub implementation - Phase 1
    dependency_cache_[key.GetCombinedHash()] = execution_order;
  }

  //! Get cached dependency graph
  [[nodiscard]] virtual auto GetDependencyGraph(const RenderGraphCacheKey& key)
    -> std::optional<std::vector<PassHandle>>
  {
    auto it = dependency_cache_.find(key.GetCombinedHash());
    if (it != dependency_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  //! Cache validation results
  virtual auto CacheValidationResults(
    const RenderGraphCacheKey& key, bool is_valid) -> void
  {
    validation_cache_[key.GetCombinedHash()] = is_valid;
  }

  //! Get cached validation results
  [[nodiscard]] virtual auto GetValidationResults(
    const RenderGraphCacheKey& key) -> std::optional<bool>
  {
    auto it = validation_cache_.find(key.GetCombinedHash());
    if (it != validation_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  //! Invalidate all caches for a key
  virtual auto InvalidateKey(const RenderGraphCacheKey& key) -> void
  {
    auto hash = key.GetCombinedHash();
    dependency_cache_.erase(hash);
    validation_cache_.erase(hash);
  }

  //! Clear all cached data
  virtual auto Clear() -> void
  {
    dependency_cache_.clear();
    validation_cache_.clear();
  }

  //! Get debug information
  [[nodiscard]] virtual auto GetDebugInfo() const -> std::string
  {
    return "CompilationCache: " + std::to_string(dependency_cache_.size())
      + " dependency graphs, " + std::to_string(validation_cache_.size())
      + " validation results";
  }

protected:
  std::unordered_map<uint64_t, std::vector<PassHandle>> dependency_cache_;
  std::unordered_map<uint64_t, bool> validation_cache_;
};

//! Deterministic hash computation utilities
namespace cache_utils {

  //! Compute deterministic hash for viewport configuration
  [[nodiscard]] inline auto ComputeViewportHash(
    const std::vector<ViewContext>& views) -> uint64_t
  {
    // Stub implementation - Phase 1
    uint64_t hash = 0;
    for (const auto& view : views) {
      // Simple hash combination - real implementation would be more robust
      hash ^= std::hash<std::string> {}(view.view_name);
    }
    return hash;
  }

  //! Compute deterministic hash for resource configuration
  [[nodiscard]] inline auto ComputeResourceHash(
    const std::vector<ResourceHandle>& resources) -> uint64_t
  {
    // Stub implementation - Phase 1
    uint64_t hash = 0;
    for (const auto& resource : resources) {
      hash ^= resource.get();
    }
    return hash;
  }

  //! Compute deterministic hash for graph structure
  [[nodiscard]] inline auto ComputeStructureHash(
    const std::vector<PassHandle>& passes) -> uint64_t
  {
    // Stub implementation - Phase 1
    uint64_t hash = 0;
    for (const auto& pass : passes) {
      hash ^= pass.get();
    }
    return hash;
  }

} // namespace cache_utils

} // namespace oxygen::examples::asyncsim

// Hash specialization for cache keys
namespace std {
template <> struct hash<oxygen::examples::asyncsim::RenderGraphCacheKey> {
  auto operator()(
    const oxygen::examples::asyncsim::RenderGraphCacheKey& key) const -> size_t
  {
    return static_cast<size_t>(key.GetCombinedHash());
  }
};
} // namespace std
