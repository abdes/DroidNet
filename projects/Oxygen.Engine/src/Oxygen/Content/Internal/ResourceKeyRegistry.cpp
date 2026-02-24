//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/ResourceKeyRegistry.h>
#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::content::internal {

auto ResourceKeyRegistry::InsertOrAssign(
  const uint64_t hash, const ResourceKey key) -> void
{
  mapping_.insert_or_assign(hash, key);
}

auto ResourceKeyRegistry::Erase(const uint64_t hash) -> void
{
  mapping_.erase(hash);
}

auto ResourceKeyRegistry::Clear() -> void { mapping_.clear(); }

auto ResourceKeyRegistry::Find(const uint64_t hash) const
  -> std::optional<ResourceKey>
{
  const auto it = mapping_.find(hash);
  if (it == mapping_.end()) {
    return std::nullopt;
  }
  return it->second;
}

auto ResourceKeyRegistry::Entries() const
  -> const std::unordered_map<uint64_t, ResourceKey>&
{
  return mapping_;
}

auto ResourceKeyRegistry::Empty() const -> bool { return mapping_.empty(); }

auto ResourceKeyRegistry::Size() const -> size_t { return mapping_.size(); }

auto ResourceKeyRegistry::AssertConsistency(std::string_view context,
  const std::function<uint64_t(ResourceKey)>& hash_resource,
  const std::function<bool(uint64_t)>& cache_contains) const -> void
{
#if !defined(NDEBUG)
  for (const auto& [stored_hash, key] : mapping_) {
    const auto expected_hash = hash_resource(key);
    if (expected_hash == stored_hash) {
      continue;
    }

    if (cache_contains(stored_hash)) {
      continue;
    }

    LOG_F(ERROR,
      "[invariant:{}] stale uncached resource mapping detected: "
      "stored_hash=0x{:016x} expected_hash=0x{:016x} key={}",
      context, stored_hash, expected_hash, to_string(key));
  }
#else
  static_cast<void>(context);
  static_cast<void>(hash_resource);
  static_cast<void>(cache_contains);
#endif
}

} // namespace oxygen::content::internal
