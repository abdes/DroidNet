//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>

#include <Oxygen/Composition/Typed.h>

namespace oxygen {

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

//! Concept for eviction policy types used in AnyCache
/*!
  ### Implementation Notes

  - Cost estimation provides hints for eviction decisions and drives the
    decision to start eviction. A cachae has a budget that is the maximum
    allowed cost for all items in the cache. As items are added, the `consumed`
    cost is updated. If the budget is exceeded, eviction is triggered.

  - `Fit` is called when eviction is needed. It can use the provided `erase_map`
    to remove items from the cache. Eviction policies decide how to evict items
    based on their own criteria, such as least recently used, least frequently
    used, or custom logic.

  @see AnyCache
*/
template <typename Eviction, typename K>
concept EvictionPolicyType = requires(Eviction e,
  typename Eviction::EntryType entry, typename Eviction::IteratorType it, K key,
  const std::shared_ptr<void>& value, TypeId type_id, bool dirty,
  typename Eviction::CostType budget, std::function<void(const K&)> erase_map) {
  // clang-format off
  { Eviction(budget) };
  { e.Clear() };
  { e.Store(std::move(entry)) } -> std::same_as<typename Eviction::IteratorType>;
  { e.TryReplace(it, value) } -> std::same_as<bool>;
  { e.Evict(it) } -> std::same_as<bool>;
  { e.CheckIn(it) };
  { e.CheckOut(it) };
  { e.Fit(std::move(erase_map)) };
  { e.Cost(value, type_id) } -> std::convertible_to<std::size_t>;
  { e.budget } -> std::convertible_to<typename Eviction::CostType>;
  { e.consumed } -> std::convertible_to<typename Eviction::CostType>;
  // clang-format on
};

/*!
  RefCountedEviction is an eviction policy for the cache that uses reference
  counting to manage item lifetimes.

  @tparam K Key type.

  ### Key Features

  - Reference-counted eviction: items are only evicted when not checked out.
  - Iterator stability: uses std::list to ensure iterators remain valid except
    for erased elements.
  - Pluggable cost function for flexible resource accounting and cache budget
    enforcement. The default cost function returns `1` for each item.

  ### Architecture Notes

  - Never erase or modify the eviction list except through the provided API.
  - Do not share the eviction list between multiple caches.

  @see AnyCache
*/
template <typename K> struct RefCountedEviction {
  using KeyType = K;
  using ValueType = std::shared_ptr<void>;
  using RefcountType = std::size_t;
  using EntryType = std::tuple<KeyType, TypeId, ValueType, RefcountType>;
  using ContainerType = std::list<EntryType>;
  using IteratorType = typename ContainerType::iterator;
  using CostType = std::size_t;
  using CostFunction
    = std::function<size_t(const std::shared_ptr<void>&, TypeId)>;

  CostType budget;
  CostType consumed { 0 };
  ContainerType eviction_list;

  // Cost estimator: uses user-provided function if set, else returns 1
  size_t Cost(const std::shared_ptr<void>& value, TypeId type_id) const
  {
    if (cost_fn_)
      return cost_fn_(value, type_id);
    return 1;
  }

  explicit RefCountedEviction(CostType _budget, CostFunction cost_fn = {})
    : budget(_budget)
    , cost_fn_(std::move(cost_fn))
  {
  }

  void Clear()
  {
    eviction_list.clear();
    consumed = 0;
  }

  IteratorType Store(EntryType&& entry)
  {
    // We consider the caller to be a user of the item
    std::get<3>(entry) = 1; // set refcount to 1
    auto& value = std::get<2>(entry);
    auto type_id = std::get<1>(entry);
    CostType item_cost = Cost(value, type_id);
    if (budget && consumed + item_cost > budget) {
      // Would exceed budget, do not store
      return eviction_list.end();
    }
    eviction_list.emplace_front(std::move(entry));
    consumed += item_cost;
    return eviction_list.begin();
  }

  bool Evict(IteratorType& it)
  {
    if (std::get<3>(*it) == 1) {
      auto& value = std::get<2>(*it);
      auto type_id = std::get<1>(*it);
      consumed -= Cost(value, type_id);
      it = eviction_list.erase(it);
      return true;
    }
    return false;
  }

  void CheckOut(IteratorType& it) { ++std::get<3>(*it); }

  void CheckIn(IteratorType& it)
  {
    auto& refcnt = std::get<3>(*it);
    if (refcnt > 0) {
      --refcnt;
      if (refcnt == 0) {
        auto& value = std::get<2>(*it);
        auto type_id = std::get<1>(*it);
        consumed -= Cost(value, type_id);
        it = eviction_list.erase(it);
      }
    }
  }

  void Fit(std::function<void(const K&)> /*erase_map*/)
  {
    // This policy only evicts based on ref count, so we do not need to
    // implement this method.
  }

  auto TryReplace(IteratorType& it, const std::shared_ptr<void>& value) -> bool
  {
    // Check if the item can be replaced (refcount == 1)
    if (std::get<3>(*it) == 1) {
      std::get<2>(*it) = value; // replace value
      return true;
    }
    return false;
  }

private:
  // User-pluggable cost function: takes value and type_id, returns cost
  // Set only at construction for consistency of consumed accounting.
  CostFunction cost_fn_;
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

  - Store and retrieve shared objects by key in multi-threaded systems.
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

  //! Construct a cache with a given budget.
  explicit AnyCache(typename EvictionPolicyType::CostType budget
    = std::numeric_limits<typename EvictionPolicyType::CostType>::max())
    : eviction_(budget)
  {
    if (budget == 0) {
      throw std::invalid_argument("Cache budget must be > 0");
    }
  }

  /*!
    Store a value in the cache, inserting or replacing by key.

    @tparam V Value type (must satisfy CacheValueType).
    @param key The key to store under.
    @param value The value to store.
    @return True if stored or replaced, false if rejected by eviction policy.

    ### Performance Characteristics

    - Time Complexity: O(1) average (hash map insert/replace).
    - Memory: May trigger eviction if budget exceeded.

    @see Replace, CheckOut, EvictionPolicyType
  */
  template <CacheValueType V> bool Store(const KeyType& key, const V& value)
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    auto type_id = value
      ? std::remove_reference_t<decltype(*value)>::ClassTypeId()
      : kInvalidTypeId;
    std::shared_ptr<void> erased = value;
    if (it != map_.end()) {
      // Already exists: try replacing
      if (!eviction_.TryReplace(it->second, erased)) {
        return false;
      }
      // Update type id as well
      std::get<1>(*(it->second)) = type_id;
      return true;
    } else {
      EntryType entry = std::make_tuple(key, type_id, erased, 1);
      IteratorType ev_it = eviction_.Store(std::move(entry));
      if (ev_it == eviction_.eviction_list.end()) {
        // Try to fit and retry once
        eviction_.Fit([this](const KeyType& k) { this->map_.erase(k); });
        entry = std::make_tuple(key, type_id, erased, 1);
        ev_it = eviction_.Store(std::move(entry));
        if (ev_it == eviction_.eviction_list.end()) {
          return false;
        }
      }
      map_[key] = ev_it;
      return true;
    }
  }

  //! Replace an existing value by key. Returns false if key not present or not
  //! replaceable.
  template <CacheValueType V> bool Replace(const KeyType& key, const V& value)
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) {
      return false;
    }
    auto type_id = value
      ? std::remove_reference_t<decltype(*value)>::ClassTypeId()
      : kInvalidTypeId;
    std::shared_ptr<void> erased = value;
    if (!eviction_.TryReplace(it->second, erased)) {
      return false;
    }
    std::get<1>(*(it->second)) = type_id;
    return true;
  }

  /*!
    Check out (borrow) a value by key.

    @tparam V Value type (must satisfy CacheValueType).
    @param key The key to check out.
    @return Shared pointer to the value if present and type matches, else empty.

    @note Increments usage state as interpreted by the eviction policy.
    @see CheckIn, Peek
  */
  template <CacheValueType V> V CheckOut(const KeyType& key)
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      auto& entry = *(it->second);
      eviction_.CheckOut(it->second); // use the item
      TypeId stored_type = std::get<1>(entry);
      if constexpr (requires { V::element_type::ClassTypeId(); }) {
        if (stored_type == V::element_type::ClassTypeId()) {
          return std::static_pointer_cast<typename V::element_type>(
            std::get<2>(entry));
        }
      }
    }
    return V {};
  }

  /*!
    Peek at a value by key without affecting usage state.

    @tparam V Value type (must satisfy CacheValueType).
    @param key The key to peek.
    @return Shared pointer to the value if present and type matches, else empty.

    @see CheckOut
  */
  template <CacheValueType V> V Peek(const KeyType& key) const
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      auto& entry = *(it->second);
      TypeId stored_type = std::get<1>(entry);
      if constexpr (requires { V::element_type::ClassTypeId(); }) {
        if (stored_type == V::element_type::ClassTypeId()) {
          return std::static_pointer_cast<typename V::element_type>(
            std::get<2>(entry));
        }
      }
    }
    return V {};
  }

  //! Check in (return) a previously checked out value.
  void CheckIn(const KeyType& key)
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      eviction_.CheckIn(it->second);
      // If the item was evicted, remove it from the map
      if (it->second == eviction_.eviction_list.end()) {
        map_.erase(it);
      }
    }
  }

  //! Remove a value by key if permitted by the eviction policy.
  bool Remove(const KeyType& key)
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      if (eviction_.Evict(it->second)) {
        map_.erase(it);
        return true;
      }
      return false;
    }
    return false;
  }

  //! Remove all items from the cache, ignoring constraints.
  void Clear()
  {
    std::unique_lock lock(mutex_);
    eviction_.Clear();
    map_.clear();
  }

  //! Returns true if the cache contains the given key.
  bool Contains(const KeyType& key) const noexcept
  {
    std::shared_lock lock(mutex_);
    return map_.find(key) != map_.end();
  }

  //! Returns the TypeId of the value stored under the given key, or
  //! kInvalidTypeId if not present.
  TypeId GetTypeId(const KeyType& key) const noexcept
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      return std::get<1>(*(it->second));
    }
    return kInvalidTypeId;
  }

  //! Returns the current usage count for a key, or 0 if not present.
  std::size_t GetRefCount(const KeyType& key) const noexcept
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      return std::get<3>(*(it->second));
    }
    return 0;
  }

  //! Returns the number of items currently in the cache.
  std::size_t Size() const noexcept
  {
    std::shared_lock lock(mutex_);
    return map_.size();
  }

  //! Returns the current total cost consumed by all items in the cache.
  typename EvictionPolicyType::CostType Consumed() const noexcept
  {
    std::shared_lock lock(mutex_);
    return eviction_.consumed;
  }

  //! Returns the maximum allowed cost (budget) for the cache.
  typename EvictionPolicyType::CostType Budget() const noexcept
  {
    std::shared_lock lock(mutex_);
    return eviction_.budget;
  }

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
      const value_type& operator*() const { return it_->first; }
      iterator& operator++()
      {
        ++it_;
        return *this;
      }
      iterator operator++(int)
      {
        auto tmp = *this;
        ++it_;
        return tmp;
      }
      bool operator==(const iterator& other) const = default;
    };
    iterator begin() const { return iterator(map_->begin()); }
    iterator end() const { return iterator(map_->end()); }
  };

  //! brief Returns a view of all keys in the cache.
  /*!
   Provides a non-owning, non mutating, range-compatible view of all keys
   currently present in the cache.

   @return A KeysView object for iterating over cache keys.

   @warning This view and its iterators are NOT thread safe. You must hold the
   cache's lock (by not calling any other cache method from any thread) for the
   entire lifetime of the view and all iterators derived from it. If the cache
   is modified in any way (insert, remove, checkin, checkout, clear, etc.) while
   a view or its iterator is in use, the behavior is undefined and may result in
   crashes or data corruption.

   This is the same as the C++ standard library containers and views: non-owning
   views over mutable containers are inherently unsafe for concurrent use. If
   you need a thread-safe snapshot, copy the keys into a container under lock.

   ### Example Usage

   Usage of KeysView to iterate over keys in the cache is obvious, but here is
   an example of how to use it to get a view of all items of a specific
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
  mutable std::shared_mutex mutex_;
  EvictionPolicyType eviction_;
  std::unordered_map<KeyType, IteratorType, Hash> map_;
};

} // namespace oxygen
