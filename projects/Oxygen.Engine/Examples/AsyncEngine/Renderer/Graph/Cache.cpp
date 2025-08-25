//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Cache.h"

#include <Oxygen/Base/Logging.h>
#include <list>
#include <memory>
#include <mutex>

namespace oxygen::examples::asyncsim {

// Simple concrete cache implementation used by the AsyncEngine.
// Derives from the public RenderGraphCache so tests and consumers can
// program against the interface while we keep the default behavior here.
class DefaultRenderGraphCache : public RenderGraphCache {
public:
  DefaultRenderGraphCache() = default;
  ~DefaultRenderGraphCache() override = default;

  // Thread-safe LRU Get
  auto Get(const RenderGraphCacheKey& key)
    -> std::shared_ptr<RenderGraph> override
  {
    std::lock_guard lock(m_);
    auto h = key.GetCombinedHash();
    // Account for a new cache request
    stats_.total_requests++;
    auto it = index_.find(h);
    if (it == index_.end()) {
      // Miss: account and return
      stats_.misses++;
      return nullptr;
    }
    // Move to front (most-recently used)
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    auto& entry = cache_entries_[h];
    entry.access_count++;
    stats_.hits++;
    return entry.compiled_graph;
  }

  // Thread-safe Set with simple memory accounting and LRU eviction
  auto Set(const RenderGraphCacheKey& key, std::shared_ptr<RenderGraph> graph,
    const SchedulingResult& scheduling) -> void override
  {
    std::lock_guard lock(m_);
    auto h = key.GetCombinedHash();

    RenderGraphCacheEntry entry(std::move(graph));
    entry.scheduling_result = scheduling;
    // Estimate memory usage: use stored value if provided, else default
    if (entry.memory_usage == 0) {
      entry.memory_usage = 1024; // conservative default estimate
    }

    // Insert or update
    cache_entries_[h] = std::move(entry);

    // Update LRU list
    auto it = index_.find(h);
    if (it != index_.end()) {
      // already present: move iterator to front
      lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
      index_[h] = lru_list_.begin();
    } else {
      lru_list_.push_front(h);
      index_[h] = lru_list_.begin();
    }

    stats_.entries = cache_entries_.size();
    stats_.memory_usage = GetMemoryUsage();

    // Evict while limits exceeded
    while ((cache_entries_.size() > max_cache_entries_)
      || (stats_.memory_usage > max_memory_bytes_)) {
      uint64_t victim = lru_list_.back();
      lru_list_.pop_back();
      index_.erase(victim);
      cache_entries_.erase(victim);
      stats_.evictions++;
      stats_.entries = cache_entries_.size();
      stats_.memory_usage = GetMemoryUsage();
    }
  }

  auto Contains(const RenderGraphCacheKey& key) const -> bool override
  {
    std::lock_guard lock(m_);
    return index_.find(key.GetCombinedHash()) != index_.end();
  }

  auto Invalidate(const RenderGraphCacheKey& key) -> void override
  {
    std::lock_guard lock(m_);
    auto h = key.GetCombinedHash();
    auto it = index_.find(h);
    if (it != index_.end()) {
      lru_list_.erase(it->second);
      index_.erase(it);
    }
    cache_entries_.erase(h);
    stats_.entries = cache_entries_.size();
    stats_.memory_usage = GetMemoryUsage();
  }

  auto Clear() -> void override
  {
    std::lock_guard lock(m_);
    cache_entries_.clear();
    index_.clear();
    lru_list_.clear();
    stats_ = {};
  }

  [[nodiscard]] auto GetCacheStatsObj() const
    -> RenderGraphCache::RenderGraphCacheStats override
  {
    std::lock_guard lock(m_);
    return stats_;
  }

  // Use base-class LogStats implementation (defined below) to avoid duplicate
  // code.

private:
  mutable std::mutex m_;
  std::list<uint64_t> lru_list_;
  std::unordered_map<uint64_t, std::list<uint64_t>::iterator> index_;
  // reuse base class storage: cache_entries_

  // reuse base class storage: stats_ moved to base class
};

// Factory: produce default cache instance. Returns unique_ptr to the
// caller while cached entries continue to be exposed as shared_ptr so
// ownership of compiled graphs can be shared between cache and users.
auto CreateAsyncRenderGraphCache() -> std::unique_ptr<RenderGraphCache>
{
  LOG_F(3, "Creating default AsyncEngine cache");
  return std::make_unique<DefaultRenderGraphCache>();
}

auto RenderGraphCache::LogStats() const -> void
{
  auto s = GetCacheStatsObj();

  // Compute percentages (avoid division by zero)
  double hit_pct = 0.0;
  double miss_pct = 0.0;
  if (s.total_requests > 0) {
    hit_pct = 100.0 * double(s.hits) / double(s.total_requests);
    miss_pct = 100.0 * double(s.misses) / double(s.total_requests);
  }

  LOG_SCOPE_F(3, "Cache Statistics");
  LOG_F(3, "entries          : {} / {}", s.entries, max_cache_entries_);
  LOG_F(3, "memory usage (B) : {} / {}", s.memory_usage, max_memory_bytes_);
  LOG_F(3, "total requests   : {}", s.total_requests);
  LOG_F(3, "hits             : {} ({:.2f}%)", s.hits, hit_pct);
  LOG_F(3, "misses           : {} ({:.2f}%)", s.misses, miss_pct);
  LOG_F(3, "evictions        : {}", s.evictions);
}

} // namespace oxygen::examples::asyncsim
