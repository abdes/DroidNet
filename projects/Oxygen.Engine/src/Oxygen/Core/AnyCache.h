//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/CachePolicyContract.h>

namespace oxygen {

enum class CheckoutOwner : uint8_t {
  kInternal = 0,
  kExternal = 1,
};

[[nodiscard]] constexpr auto to_string(const CheckoutOwner owner) noexcept
  -> const char*
{
  switch (owner) {
  case CheckoutOwner::kInternal:
    return "internal";
  case CheckoutOwner::kExternal:
    return "external";
  }
  return "__Unknown__";
}

//! Concept for cache value types: only allows std::shared_ptr<T> where T :
//! IsTyped
/*!
  This concept ensures that cache values are shared pointers to types that
  satisfy the IsTyped concept. This allows the cache to store and manage
  heterogeneous types safely.
*/
template <typename V>
concept CacheValueType = requires {
  typename V::element_type;
  requires std::is_same_v<V, std::shared_ptr<typename V::element_type>>;
  requires IsTyped<typename V::element_type>;
};

//! Generic thread-safe heterogeneous cache class template.
/*!
  AnyCache is a flexible, type-erased, thread-safe cache for storing and
  retrieving objects by key, with pluggable eviction and a borrowed ownership
  model.

  The cache is designed for scenarios where multiple subsystems or threads
  need to share, reuse, and manage the lifetime of heterogeneous objects
  (e.g., assets, resources, or components) in a concurrent environment.

  ### Fundamental Working Model

  - **Type Erasure**: Values are stored as `std::shared_ptr<void>`, with
    runtime type information (TypeId) tracked per entry. API methods enforce
    type safety at the boundary using the `CacheValueType` concept and
    runtime checks.

  - **Borrowed Ownership Model**: The cache enforces a check-out/check-in
    protocol for item usage. When a client needs to use an object, it calls
    `CheckOut()` to borrow it, and must later call `CheckIn()` to return it.
    The cache itself does not interpret or enforce reference counting or usage
    semantics; instead, it delegates all usage tracking and eviction logic to
    the eviction policy. The eviction policy may use reference counting,
    usage timestamps, or any other mechanism to determine when an item is
    eligible for removal.

    This separation allows the cache to remain agnostic to the specific
    eviction or usage policy, supporting a wide range of resource management
    strategies.

  - **Eviction Policy**: The cache delegates eviction logic to a pluggable
    policy (e.g., `RefCountedEviction`), which manages resource budgets,
    cost estimation, and removal of unused items. The eviction policy is
    responsible for interpreting check-out/check-in events and deciding when
    items can be evicted.

  - **Thread Safety**: All operations are protected by a shared mutex,
    allowing concurrent reads and exclusive writes. Views (e.g., KeysView)
    are not thread-safe and require external synchronization.

  ### Key Features

  - Thread-safe access and mutation.
  - Type-erased storage with runtime type checking.
  - Borrowed ownership model with pluggable usage/eviction policy.
  - Range-compatible key views for iteration.

  ### Usage Patterns

  - Store and retrieve shared objects by key in multithreaded systems.
  - Use with custom eviction policies for different resource constraints.
  - Integrate with asset/resource managers, component systems, or service
    registries requiring safe, concurrent object caching.

  @warning Views (e.g., KeysView) are not thread-safe and require external
  synchronization for safe use.
  @see RefCountedEviction, CacheValueType, EvictionPolicyType
*/
template <typename Key, typename Evict, typename Hash = std::hash<Key>>
  requires EvictionPolicyType<Evict, Key>
class AnyCache {
public:
  using KeyType = Key;
  using EvictionPolicyType = Evict;
  using EntryType = typename EvictionPolicyType::EntryType;
  using IteratorType = typename EvictionPolicyType::IteratorType;
  using CostType = typename EvictionPolicyType::CostType;

  struct Stats final {
    std::size_t size { 0 };
    CostType budget { 0 };
    CostType consumed { 0 };
    std::size_t checked_out_items { 0 };
    std::size_t checked_out_internal { 0 };
    std::size_t checked_out_external { 0 };
    std::size_t pinned_internal { 0 };
    std::size_t pinned_external { 0 };
    std::size_t total_checkouts { 0 };
    bool over_budget { false };
  };

  //! Construct a cache with a given budget.
  explicit AnyCache(CostType budget = (std::numeric_limits<CostType>::max)())
    : eviction_(budget)
  {
    if (budget == 0) {
      throw std::invalid_argument("Cache budget must be > 0");
    }
  }

  /*!
    Store a value in the cache, inserting or replacing by key.

    @tparam V Value type (must satisfy CacheValueType concept).
    @param key The key to store under.
    @param value The value to store.
    @return True if stored or replaced, false if rejected by eviction policy.

    ### Performance Characteristics

    - Time Complexity: O(1) average (hash map insert/replace).
    - Memory: May trigger eviction if budget exceeded.

    @see Replace, CheckOut, EvictionPolicyType
  */
  template <CacheValueType V> auto Store(const KeyType& key, V value) -> bool
  {
    std::vector<EntryType> evicted;
    bool stored = false;
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    auto type_id = value
      ? std::remove_reference_t<decltype(*value)>::ClassTypeId()
      : kInvalidTypeId;
    std::shared_ptr<void> erased = std::move(value);
    if (it != map_.end()) {
      // Already exists: try replacing
      if (!eviction_.TryReplace(it->second, erased, type_id)) {
        return false;
      }
      stored = true;
      lock.unlock();
      DispatchEvictions(evicted);
      return stored;
    }
    EntryType entry = eviction_.MakeEntry(key, type_id, erased);
    IteratorType ev_it = eviction_.Store(std::move(entry));
    if (eviction_.IsEnd(ev_it)) {
      const auto incoming_cost_as_size = eviction_.Cost(erased, type_id);
      if (incoming_cost_as_size
          > static_cast<std::size_t>((std::numeric_limits<CostType>::max)())
        || static_cast<CostType>(incoming_cost_as_size) > eviction_.Budget()) {
        stored = false;
        lock.unlock();
        DispatchEvictions(evicted);
        return stored;
      }

      const auto incoming_cost = static_cast<CostType>(incoming_cost_as_size);
      const auto original_budget = eviction_.Budget();
      const auto target_budget = original_budget - incoming_cost;

      // Force an eviction pass that creates room for the incoming item.
      eviction_.SetBudget(target_budget);
      static_cast<void>(
        eviction_.EnforceBudget([this, &evicted](const KeyType& k) {
          const auto doomed = map_.find(k);
          if (doomed != map_.end()) {
            evicted.push_back(eviction_.MakeEntry(doomed->first,
              eviction_.TypeOf(doomed->second),
              eviction_.ValueOf(doomed->second)));
            map_.erase(doomed);
          }
        }));
      eviction_.SetBudget(original_budget);

      entry = eviction_.MakeEntry(key, type_id, erased);
      ev_it = eviction_.Store(std::move(entry));
      if (eviction_.IsEnd(ev_it)) {
        stored = false;
        lock.unlock();
        DispatchEvictions(evicted);
        return stored;
      }
    }
    map_[key] = ev_it;
    stored = true;
    lock.unlock();
    DispatchEvictions(evicted);
    return stored;
  }

  //! Replace an existing value by key. Returns false if key not present or not
  //! replaceable.
  template <CacheValueType V>
  auto Replace(const KeyType& key, const V& value) -> bool
  {
    std::optional<EntryType> replaced_entry;
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) {
      return false;
    }
    auto type_id = value
      ? std::remove_reference_t<decltype(*value)>::ClassTypeId()
      : kInvalidTypeId;
    std::shared_ptr<void> erased = value;
    replaced_entry = eviction_.MakeEntry(
      key, eviction_.TypeOf(it->second), eviction_.ValueOf(it->second));
    if (eviction_.TryReplace(it->second, erased, type_id)) {
      lock.unlock();
      if (on_eviction_ && replaced_entry.has_value()) {
        on_eviction_(eviction_.EntryKey(*replaced_entry),
          eviction_.EntryValue(*replaced_entry),
          eviction_.EntryTypeId(*replaced_entry));
      }
      return true;
    }
    return false;
  }

  //! Check out (borrow) a value by key.
  /*!
    @tparam V Value type (must satisfy IsTyped concept).
    @param key The key to check out.
    @return Shared pointer to the value if present and type matches, else empty.

    @see CheckIn, Peek
  */
  template <IsTyped V>
  auto CheckOut(const KeyType& key, const CheckoutOwner owner)
    -> std::shared_ptr<V>
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      eviction_.CheckOut(it->second);
      auto& owner_counts = owner_counts_[key];
      if (owner == CheckoutOwner::kInternal) {
        ++owner_counts.checkout_internal;
      } else {
        ++owner_counts.checkout_external;
      }
      LOG_F(1, "AnyCache::CheckOut owner={} hit=true", to_string(owner));
      TypeId stored_type = eviction_.TypeOf(it->second);
      if constexpr (requires { V::ClassTypeId(); }) {
        if (stored_type == V::ClassTypeId()) {
          return std::static_pointer_cast<V>(eviction_.ValueOf(it->second));
        }
      }
    }
    LOG_F(1, "AnyCache::CheckOut owner={} hit=false", to_string(owner));
    return {};
  }

  //! Mark an item as checked out without returning it.
  /*!
   This method has the same effect as the strongly typed CheckOut method, but
   can be used when you simply need to mark an item as in use without
   retrieving it. This is similar to touching a file to update its stats without
   actually accessing its contents.
  */
  auto Touch(const KeyType& key, const CheckoutOwner owner) -> void
  {
    static_cast<void>(Pin(key, owner));
  }

  //! Mark an item as resident/in-use without retrieving it.
  /*!
    @return true if the key exists and was pinned, false otherwise.
  */
  auto Pin(const KeyType& key, const CheckoutOwner owner) -> bool
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      eviction_.CheckOut(it->second);
      auto& owner_counts = owner_counts_[key];
      if (owner == CheckoutOwner::kInternal) {
        ++owner_counts.pin_internal;
      } else {
        ++owner_counts.pin_external;
      }
      LOG_F(1, "AnyCache::Pin owner={} hit=true", to_string(owner));
      return true;
    }
    LOG_F(1, "AnyCache::Pin owner={} hit=false", to_string(owner));
    return false;
  }

  /*!
    Peek at a value by key without affecting usage state.

    @tparam V Value type (must satisfy IsTyped concept).
    @param key The key to peek.
    @return Shared pointer to the value if present and type matches, else empty.

    @see CheckOut
  */
  template <IsTyped V> auto Peek(const KeyType& key) const -> std::shared_ptr<V>
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      TypeId stored_type = eviction_.TypeOf(it->second);
      if constexpr (requires { V::ClassTypeId(); }) {
        if (stored_type == V::ClassTypeId()) {
          return std::static_pointer_cast<V>(eviction_.ValueOf(it->second));
        }
      }
    }
    return {};
  }

  //! Check in (return) a previously checked out value.
  auto CheckIn(const KeyType& key) -> void
  {
    std::optional<EntryType> evicted_entry;
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      if (auto owner_it = owner_counts_.find(key);
        owner_it != owner_counts_.end()) {
        auto& counts = owner_it->second;
        if (counts.checkout_external > 0U) {
          --counts.checkout_external;
        } else if (counts.checkout_internal > 0U) {
          --counts.checkout_internal;
        } else if (counts.pin_external > 0U) {
          --counts.pin_external;
        } else if (counts.pin_internal > 0U) {
          --counts.pin_internal;
        }
        if (counts.checkout_internal == 0U && counts.checkout_external == 0U
          && counts.pin_internal == 0U && counts.pin_external == 0U) {
          owner_counts_.erase(owner_it);
        }
      }
      evicted_entry = eviction_.CheckIn(it->second);
      if (evicted_entry.has_value()) {
        map_.erase(it);
        owner_counts_.erase(key);
      }
      LOG_F(1, "AnyCache::CheckIn hit=true");
    } else {
      LOG_F(1, "AnyCache::CheckIn hit=false");
    }
    lock.unlock();
    if (on_eviction_ && evicted_entry.has_value()) {
      on_eviction_(eviction_.EntryKey(*evicted_entry),
        eviction_.EntryValue(*evicted_entry),
        eviction_.EntryTypeId(*evicted_entry));
    }
  }

  //! Release one resident usage.
  /*!
    @return true when an active pin/checkout was released; false when key is
    missing or no active checkout is tracked.
  */
  auto Unpin(const KeyType& key) -> bool
  {
    std::optional<EntryType> evicted_entry;
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) {
      LOG_F(1, "AnyCache::Unpin hit=false released=false");
      return false;
    }
    if (eviction_.RefCountOf(it->second) == 0) {
      LOG_F(1, "AnyCache::Unpin hit=true released=false");
      return false;
    }
    if (auto owner_it = owner_counts_.find(key);
      owner_it != owner_counts_.end()) {
      auto& counts = owner_it->second;
      if (counts.pin_external > 0U) {
        --counts.pin_external;
      } else if (counts.pin_internal > 0U) {
        --counts.pin_internal;
      } else if (counts.checkout_external > 0U) {
        --counts.checkout_external;
      } else if (counts.checkout_internal > 0U) {
        --counts.checkout_internal;
      }
      if (counts.checkout_internal == 0U && counts.checkout_external == 0U
        && counts.pin_internal == 0U && counts.pin_external == 0U) {
        owner_counts_.erase(owner_it);
      }
    }
    evicted_entry = eviction_.CheckIn(it->second);
    if (evicted_entry.has_value()) {
      map_.erase(it);
      owner_counts_.erase(key);
    }
    LOG_F(1, "AnyCache::Unpin hit=true released=true");
    lock.unlock();
    if (on_eviction_ && evicted_entry.has_value()) {
      on_eviction_(eviction_.EntryKey(*evicted_entry),
        eviction_.EntryValue(*evicted_entry),
        eviction_.EntryTypeId(*evicted_entry));
    }
    return true;
  }

  //! Remove a value by key if permitted by the eviction policy.
  auto Remove(const KeyType& key) -> bool
  {
    std::optional<EntryType> evicted_entry;
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      evicted_entry = eviction_.Evict(it->second);
      if (evicted_entry.has_value()) {
        owner_counts_.erase(key);
        map_.erase(it);
        lock.unlock();
        if (on_eviction_) {
          on_eviction_(eviction_.EntryKey(*evicted_entry),
            eviction_.EntryValue(*evicted_entry),
            eviction_.EntryTypeId(*evicted_entry));
        }
        return true;
      }
      return false;
    }
    return false;
  }

  //! Remove all items from the cache, ignoring constraints.
  auto Clear() -> void
  {
    std::vector<EntryType> evicted;
    std::unique_lock lock(mutex_);
    if (on_eviction_) {
      evicted.reserve(map_.size());
      for (auto& [key, it] : map_) {
        evicted.push_back(eviction_.MakeEntry(
          key, eviction_.TypeOf(it), eviction_.ValueOf(it)));
      }
    }
    eviction_.Clear();
    map_.clear();
    owner_counts_.clear();
    lock.unlock();

    if (on_eviction_) {
      for (const auto& entry : evicted) {
        on_eviction_(eviction_.EntryKey(entry), eviction_.EntryValue(entry),
          eviction_.EntryTypeId(entry));
      }
    }
  }

  //! Returns true if the cache contains the given key.
  auto Contains(const KeyType& key) const noexcept -> bool
  {
    std::shared_lock lock(mutex_);
    return map_.contains(key);
  }

  //! Returns the TypeId of the value stored under the given key, or
  //! kInvalidTypeId if not present.
  auto GetTypeId(const KeyType& key) const noexcept -> TypeId
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      return eviction_.TypeOf(it->second);
    }
    return kInvalidTypeId;
  }

  auto IsCheckedOut(const KeyType& key) const noexcept -> bool
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    return it != map_.end() && eviction_.RefCountOf(it->second) > 0;
  }

  //! Returns the number of active checkouts for a cached item.
  /*!
    @param key The key to query.
    @return The number of active checkouts for the item, or 0 if not present or
    not checked out.

    ### Usage

    This method is primarily intended for debugging and monitoring cache usage
    patterns. It returns the current checkout count for an item, which reflects
    how many times CheckOut() or Touch() have been called minus how many times
    CheckIn() has been called.

    @see IsCheckedOut, CheckOut, CheckIn, Touch
  */
  auto GetCheckoutCount(const KeyType& key) const noexcept -> std::size_t
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    return it != map_.end() ? eviction_.RefCountOf(it->second) : 0;
  }

  //! Returns the number of items currently in the cache.
  auto Size() const noexcept -> std::size_t
  {
    std::shared_lock lock(mutex_);
    return map_.size();
  }

  //! Returns the shared ownership count for a cached entry.
  /*!
    This is the `std::shared_ptr` use count of the stored value. It can be
    used to detect cache-only entries (`use_count == 1`) during trim passes.

    @param key The key to query.
    @return The stored value's `use_count`, or 0 if the key is not present.

    ### Performance Characteristics

    - Time Complexity: $O(1)$ average lookup.
    - Memory: No additional allocations.
    - Optimization: Acquires a shared lock only.
  */
  auto GetValueUseCount(const KeyType& key) const noexcept -> std::size_t
  {
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) {
      return 0U;
    }
    return eviction_.ValueOf(it->second).use_count();
  }

  //! Returns a thread-safe snapshot of cache keys.
  /*!
    @return A vector containing the keys present at the time of the call.

    ### Performance Characteristics

    - Time Complexity: $O(n)$ over cached items.
    - Memory: $O(n)$ for the snapshot.
    - Optimization: Reserves capacity before copying.

    @note This method acquires a shared lock for the duration of the copy.
  */
  auto KeysSnapshot() const -> std::vector<KeyType>
  {
    std::shared_lock lock(mutex_);
    std::vector<KeyType> keys;
    keys.reserve(map_.size());
    for (const auto& [key, it] : map_) {
      static_cast<void>(it);
      keys.push_back(key);
    }
    return keys;
  }

  //! Returns the current total cost consumed by all items in the cache.
  auto Consumed() const noexcept -> CostType
  {
    std::shared_lock lock(mutex_);
    return eviction_.Consumed();
  }

  //! Returns the maximum allowed cost (budget) for the cache.
  auto Budget() const noexcept -> CostType
  {
    std::shared_lock lock(mutex_);
    return eviction_.Budget();
  }

  //! Returns true when consumed cost exceeds budget.
  auto IsOverBudget() const noexcept -> bool
  {
    std::shared_lock lock(mutex_);
    return eviction_.Consumed() > eviction_.Budget();
  }

  //! Returns number of keys currently tracked as checked out.
  auto CheckedOutItemCount() const noexcept -> std::size_t
  {
    std::shared_lock lock(mutex_);
    std::size_t count = 0;
    for (const auto& [key, it] : map_) {
      static_cast<void>(key);
      if (eviction_.RefCountOf(it) > 0) {
        ++count;
      }
    }
    return count;
  }

  //! Returns a snapshot of cache residency metrics.
  auto SnapshotStats() const -> Stats
  {
    std::shared_lock lock(mutex_);
    std::size_t total_checkouts = 0;
    std::size_t checked_out_items = 0;
    std::size_t checked_out_internal = 0;
    std::size_t checked_out_external = 0;
    std::size_t pinned_internal = 0;
    std::size_t pinned_external = 0;
    for (const auto& [key, it] : map_) {
      if (const auto owner_it = owner_counts_.find(key);
        owner_it != owner_counts_.end()) {
        const auto& counts = owner_it->second;
        if (counts.checkout_internal > 0U) {
          ++checked_out_internal;
        }
        if (counts.checkout_external > 0U) {
          ++checked_out_external;
        }
        if (counts.pin_internal > 0U) {
          ++pinned_internal;
        }
        if (counts.pin_external > 0U) {
          ++pinned_external;
        }
      }
      const auto refs = eviction_.RefCountOf(it);
      total_checkouts += refs;
      if (refs > 0) {
        ++checked_out_items;
      }
    }
    return Stats {
      .size = map_.size(),
      .budget = eviction_.Budget(),
      .consumed = eviction_.Consumed(),
      .checked_out_items = checked_out_items,
      .checked_out_internal = checked_out_internal,
      .checked_out_external = checked_out_external,
      .pinned_internal = pinned_internal,
      .pinned_external = pinned_external,
      .total_checkouts = total_checkouts,
      .over_budget = eviction_.Consumed() > eviction_.Budget(),
    };
  }

  //! Sets a new budget for the cache and performs eviction if necessary.
  auto SetBudget(CostType budget) -> PolicyBudgetStatus
  {
    if (budget == 0) {
      throw std::invalid_argument("Cache budget must be > 0");
    }
    std::vector<EntryType> evicted;
    std::unique_lock lock(mutex_);
    eviction_.SetBudget(budget);
    auto status = eviction_.EnforceBudget([this, &evicted](const KeyType& k) {
      const auto it = map_.find(k);
      if (it != map_.end()) {
        evicted.push_back(eviction_.MakeEntry(it->first,
          eviction_.TypeOf(it->second), eviction_.ValueOf(it->second)));
        owner_counts_.erase(it->first);
        map_.erase(it);
      }
    });
    lock.unlock();

    if (on_eviction_) {
      for (const auto& entry : evicted) {
        on_eviction_(eviction_.EntryKey(entry), eviction_.EntryValue(entry),
          eviction_.EntryTypeId(entry));
      }
    }
    return status;
  }

  [[nodiscard]] auto GetPolicy() noexcept -> Evict& { return eviction_; }
  [[nodiscard]] auto GetPolicy() const noexcept -> const Evict&
  {
    return eviction_;
  }

  //=== Eviction Notification ===---------------------------------------------//

  using EvictionCallbackFunction
    = std::function<void(const KeyType&, std::shared_ptr<void>, TypeId)>;

  class EvictionNotificationScope {
  public:
    EvictionNotificationScope(AnyCache& cache, EvictionCallbackFunction cb)
      : cache_(&cache)
      , prev_([&] {
        std::swap(cache.on_eviction_, cb);
        return std::move(cb);
      }())
    {
    }

    ~EvictionNotificationScope()
    {
      if (cache_ != nullptr) {
        cache_->on_eviction_ = std::move(prev_);
      }
    }

    OXYGEN_MAKE_NON_COPYABLE(EvictionNotificationScope)
    OXYGEN_DEFAULT_MOVABLE(EvictionNotificationScope)

  private:
    observer_ptr<AnyCache> cache_;
    EvictionCallbackFunction prev_;
  };

  [[nodiscard]] auto OnEviction(EvictionCallbackFunction cb)
    -> EvictionNotificationScope
  {
    return EvictionNotificationScope(*this, std::move(cb));
  }

  //=== Views ===-------------------------------------------------------------//

  //! Fully std::ranges-compatible immutable Keys view
  template <typename MapType>
  class KeysView : public std::ranges::view_interface<KeysView<MapType>> {
    const MapType* map_;

  public:
    explicit KeysView(const MapType& map)
      : map_(&map)
    {
    }
    class iterator {
      using base_iter = typename MapType::const_iterator;
      base_iter it_;

    public:
      using iterator_concept = std::forward_iterator_tag;
      using value_type = typename MapType::key_type;
      using difference_type = std::ptrdiff_t;
      iterator() = default;
      explicit iterator(base_iter it)
        : it_(it)
      {
      }
      auto operator*() const -> const value_type& { return it_->first; }
      auto operator++() -> iterator&
      {
        ++it_;
        return *this;
      }
      auto operator++(int) -> iterator
      {
        auto tmp = *this;
        ++it_;
        return tmp;
      }
      auto operator==(const iterator& other) const -> bool = default;
    };
    [[nodiscard]] auto begin() const -> iterator
    {
      return iterator(map_->begin());
    }
    [[nodiscard]] auto end() const -> iterator { return iterator(map_->end()); }
  };

  //! brief Returns a view of all keys in the cache.
  /*!
   Provides a non-owning, non mutating, range-compatible view of all keys
   currently present in the cache.

   @return A KeysView object for iterating over cache keys.

   @warning This view and its iterators are NOT thread safe. You must hold the
   cache's lock (by not calling any other cache method from any thread) for the
   entire lifetime of the view and all iterators derived from it. If the cache
   is modified in any way (insert, remove, check in, check out, clear, etc.)
   while a view or its iterator is in use, the behavior is undefined and may
   result in crashes or data corruption.

   This is the same as the C++ standard library containers and views: non-owning
   views over mutable containers are inherently unsafe for concurrent use. If
   you need a thread-safe snapshot, copy the keys into a container under lock.

   ### Example Usage

   The basic usage of KeysView to iterate over keys in the cache is obvious, but
   here is an example of how to use it to get a view of all items of a specific
   type in the cache:

   ```cpp
   using MyTypePtr = std::shared_ptr<MyType>;
   auto type_id = MyType::ClassTypeId();

   auto cached_items = cache.Keys()
     | std::views::filter([&](const std::string& key) {
         return cache.GetTypeId(key) == type_id;
       })
     | std::views::transform([&](const std::string& key) {
         return cache.Peek<MyTypePtr>(key);
       })
     | std::views::filter([](const MyTypePtr& ptr) {
         return static_cast<bool>(ptr);
       });

   // Now you can iterate:
   for (const MyTypePtr& item : cached_items) {
     // Use *item
   }
   ```

   @see KeysView
  */
  auto Keys() const
  {
    return KeysView<std::unordered_map<KeyType, IteratorType, Hash>>(map_);
  }

private:
  auto DispatchEvictions(const std::vector<EntryType>& entries) -> void
  {
    if (!on_eviction_) {
      return;
    }
    for (const auto& entry : entries) {
      on_eviction_(eviction_.EntryKey(entry), eviction_.EntryValue(entry),
        eviction_.EntryTypeId(entry));
    }
  }

  mutable std::shared_mutex mutex_;
  EvictionPolicyType eviction_;
  std::unordered_map<KeyType, IteratorType, Hash> map_;
  struct OwnerCounts final {
    uint32_t checkout_internal { 0 };
    uint32_t checkout_external { 0 };
    uint32_t pin_internal { 0 };
    uint32_t pin_external { 0 };
  };
  std::unordered_map<KeyType, OwnerCounts, Hash> owner_counts_;
  EvictionCallbackFunction on_eviction_;
};

} // namespace oxygen
