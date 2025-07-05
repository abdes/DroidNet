//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/ContentCache.h>

namespace oxygen::content {

auto ContentCache::IncrementRefCount(uint64_t hash_key) -> bool
{
  std::lock_guard lock(cache_mutex_);

  auto it = cache_.find(hash_key);
  if (it == cache_.end()) {
    return false; // Key not found
  }

  it->second.ref_count++;
  return true;
}

auto ContentCache::DecrementRefCount(uint64_t hash_key) -> bool
{
  std::lock_guard lock(cache_mutex_);

  auto it = cache_.find(hash_key);
  if (it == cache_.end()) {
    return false; // Key not found
  }

  it->second.ref_count--;

  // Remove entry if reference count reaches zero
  if (it->second.ref_count <= 0) {
    cache_.erase(it);
  }

  return true;
}

auto ContentCache::GetRefCount(uint64_t hash_key) const -> int
{
  std::lock_guard lock(cache_mutex_);

  auto it = cache_.find(hash_key);
  if (it == cache_.end()) {
    return 0; // Key not found
  }

  return it->second.ref_count;
}

auto ContentCache::Remove(uint64_t hash_key) -> bool
{
  std::lock_guard lock(cache_mutex_);

  auto it = cache_.find(hash_key);
  if (it == cache_.end()) {
    return false; // Key not found
  }

  cache_.erase(it);
  return true;
}

void ContentCache::Clear()
{
  std::lock_guard lock(cache_mutex_);
  cache_.clear();
}

auto ContentCache::Size() const -> size_t
{
  std::lock_guard lock(cache_mutex_);
  return cache_.size();
}

} // namespace oxygen::content
