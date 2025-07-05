//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content {

//! Generic content cache with type safety and manual reference-counted eviction
/*!
 A generic cache that can store any content type that satisfies the `IsTyped`
 concept. Uses uint64_t hash keys for efficient storage and lookup, with
 automatic type safety validation through the type system.

 ### Key Features

 - **Type Safety**: Uses IsTyped concept and TypeId validation
 - **Efficient**: uint64_t hash keys for fast lookup
 - **Generic**: Works with any content type (assets, resources, etc.)
 - **Thread Safe**: Mutex-protected operations
 - **Manual Reference Counting**: Cache entries are retained as long as their
   reference count is above zero. When the reference count reaches zero,
   the entry is automatically evicted from the cache.

 ### Cache Eviction

 Each cached entry is associated with a manual reference count. The reference
 count is incremented and decremented explicitly via `IncrementRefCount` and
 `DecrementRefCount`. When `DecrementRefCount` reduces the count to zero,
 the entry is immediately removed from the cache. This ensures that only
 actively referenced content remains cached, and unused content is evicted
 promptly.

 ### Usage Examples

 ```cpp
 ContentCache cache;

 // Cache an asset
 auto asset = std::make_shared<GeometryAsset>();
 uint64_t asset_key = HashAssetKey(key);
 cache.Store<GeometryAsset>(asset_key, asset);

 // Cache a resource
 auto resource = std::make_shared<BufferResource>();
 uint64_t resource_key = HashResourceKey(pak_index, resource_id);
 cache.Store<BufferResource>(resource_key, resource);

 // Retrieve with type safety
 auto cached_asset = cache.Get<GeometryAsset>(asset_key);
 auto cached_resource = cache.Get<BufferResource>(resource_key);
 ```

 @note All stored types must satisfy the IsTyped concept
 @warning Type mismatches return nullptr rather than throwing
 @see IsTyped, TypeId
*/
class ContentCache {
public:
  ContentCache() = default;
  ~ContentCache() = default;

  OXYGEN_MAKE_NON_COPYABLE(ContentCache)
  OXYGEN_DEFAULT_MOVABLE(ContentCache)

  //! Store content in the cache with type safety
  /*!
   Stores content in the cache using a hash key. The content type must satisfy
   the IsTyped concept for automatic type validation.

   @tparam T The content type (must satisfy IsTyped)
   @param hash_key The uint64_t hash key for this content
   @param content The content to store
   @param initial_ref_count Initial reference count (default: 1)

   ### Performance Characteristics
   - Time Complexity: O(1) average, O(log n) worst case
   - Memory: Stores shared_ptr with type-erased wrapper
   - Thread Safety: Mutex-protected operation

   @note Overwrites existing content with the same key
   @see Get, Has, Release
  */
  template <IsTyped T>
  auto Store(uint64_t hash_key, std::shared_ptr<T> content,
    int initial_ref_count = 1) -> void;

  //! Retrieve content from cache with type safety
  /*!
   Retrieves content from the cache, performing automatic type validation.
   Returns nullptr if the key is not found or if there's a type mismatch.

   @tparam T The expected content type (must satisfy IsTyped)
   @param hash_key The uint64_t hash key to look up
   @return Shared pointer to content, or nullptr if not found/type mismatch

   ### Performance Characteristics
   - Time Complexity: O(1) average, O(log n) worst case
   - Type Safety: Validates TypeId before returning
   - Thread Safety: Mutex-protected operation

   @note Does not increment reference count
   @see Store, Has, IncrementRefCount
  */
  template <IsTyped T> auto Get(uint64_t hash_key) const -> std::shared_ptr<T>;

  //! Check if content exists in cache
  /*!
   Checks whether content with the given key exists in the cache.
   Optionally validates the expected type.

   @tparam T The expected content type (must satisfy IsTyped)
   @param hash_key The uint64_t hash key to check
   @return True if content exists and type matches, false otherwise

   @note Does not affect reference count
   @see Get, Store
  */
  template <IsTyped T> auto Has(uint64_t hash_key) const -> bool;

  //! Increment reference count for cached content
  /*!
   Increments the reference count for content in the cache. Used for
   shared ownership tracking.

   @param hash_key The uint64_t hash key of the content
   @return True if content was found and ref count incremented

   @see DecrementRefCount, GetRefCount
  */
  OXGN_CNTT_API auto IncrementRefCount(uint64_t hash_key) -> bool;

  //! Decrement reference count and potentially remove content
  /*!
   Decrements the reference count for content in the cache. If the count
   reaches zero, the content is removed from the cache.

   @param hash_key The uint64_t hash key of the content
   @return True if content was found, false if key doesn't exist

   @note Content is automatically removed when ref count reaches zero
   @see IncrementRefCount, GetRefCount
  */
  OXGN_CNTT_API auto DecrementRefCount(uint64_t hash_key) -> bool;

  //! Get current reference count for cached content
  /*!
   Returns the current reference count for content in the cache.

   @param hash_key The uint64_t hash key of the content
   @return Current reference count, or 0 if key doesn't exist

   @see IncrementRefCount, DecrementRefCount
  */
  OXGN_CNTT_API auto GetRefCount(uint64_t hash_key) const -> int;

  //! Remove content from cache regardless of reference count
  /*!
   Forcibly removes content from the cache, ignoring reference count.
   Use with caution as this can break reference counting assumptions.

   @param hash_key The uint64_t hash key of the content to remove
   @return True if content was found and removed

   @warning Breaks reference counting - use only for cleanup/shutdown
   @see DecrementRefCount
  */
  OXGN_CNTT_API auto Remove(uint64_t hash_key) -> bool;

  //! Clear all cached content
  /*!
   Removes all content from the cache, ignoring reference counts.
   Use for shutdown or testing scenarios.

   @warning Breaks all reference counting - use only for cleanup/shutdown
  */
  OXGN_CNTT_API auto Clear() -> void;

  //! Get current cache size (number of entries)
  /*!
   Returns the current number of entries in the cache.

   @return Number of cached entries
  */
  OXGN_CNTT_API auto Size() const -> size_t;

private:
  //! Internal cache entry with type information and reference counting
  struct CacheEntry {
    std::shared_ptr<void> content; //!< Type-erased content pointer
    TypeId content_type; //!< Type ID for validation
    int ref_count; //!< Reference count for lifecycle management

    CacheEntry(std::shared_ptr<void> content_ptr, TypeId type_id, int refs)
      : content(std::move(content_ptr))
      , content_type(type_id)
      , ref_count(refs)
    {
    }
  };

  mutable std::mutex cache_mutex_; //!< Mutex for thread-safe operations
  std::unordered_map<uint64_t, CacheEntry> cache_; //!< Main cache storage
};

//=== Template Implementation ============================================//

template <IsTyped T>
auto ContentCache::Store(
  uint64_t hash_key, std::shared_ptr<T> content, int initial_ref_count) -> void
{
  if (!content) {
    return; // Don't store null content
  }

  std::lock_guard lock(cache_mutex_);

  // Create type-erased entry with type validation
  auto type_id = T::ClassTypeId();
  auto void_ptr = std::static_pointer_cast<void>(content);

  cache_.insert_or_assign(
    hash_key, CacheEntry(std::move(void_ptr), type_id, initial_ref_count));
}

template <IsTyped T>
auto ContentCache::Get(uint64_t hash_key) const -> std::shared_ptr<T>
{
  std::lock_guard lock(cache_mutex_);

  auto it = cache_.find(hash_key);
  if (it == cache_.end()) {
    return nullptr; // Key not found
  }

  const auto& entry = it->second;

  // Validate type safety
  if (entry.content_type != T::ClassTypeId()) {
    return nullptr; // Type mismatch
  }

  // Cast back to typed pointer
  return std::static_pointer_cast<T>(entry.content);
}

template <IsTyped T> auto ContentCache::Has(uint64_t hash_key) const -> bool
{
  return Get<T>(hash_key) != nullptr;
}

} // namespace oxygen::content
