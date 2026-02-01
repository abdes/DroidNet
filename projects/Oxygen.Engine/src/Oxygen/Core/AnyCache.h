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
#include <vector>

#include <Oxygen/Base/Logging.h>
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
    decision to start eviction. A cache has a budget that is the maximum
    allowed cost for all items in the cache. As items are added, the `consumed`
    cost is updated. If the budget is exceeded, eviction is triggered.

  - `Evict` and `CheckIn` methods return the evicted entry when eviction occurs,
    allowing the cache to handle eviction callbacks without knowing the internal
    eviction policy logic. This maintains proper separation of concerns between
    the cache and eviction policy.

  - `Fit` is called when eviction is needed. It can use the provided `erase_map`
    to remove items from the cache. Eviction policies decide how to evict items
    based on their own criteria, such as least recently used, least frequently
    used, or custom logic.

  @see AnyCache
*/
template <typename Eviction, typename K>
concept EvictionPolicyType = requires(Eviction e, const Eviction ce,
  typename Eviction::EntryType entry, typename Eviction::IteratorType it, K key,
  const std::shared_ptr<void>& value, TypeId type_id, bool dirty,
  typename Eviction::CostType budget, std::function<void(const K&)> erase_map) {
  // clang-format off
  { Eviction(budget) };
  { e.Clear() };
  { e.Store(std::move(entry)) } -> std::same_as<typename Eviction::IteratorType>;
  { e.TryReplace(it, value) } -> std::same_as<bool>;
  { e.Evict(it) } -> std::same_as<std::optional<typename Eviction::EntryType>>;
  { e.CheckIn(it) } -> std::same_as<std::optional<typename Eviction::EntryType>>;
  { e.CheckOut(it) };
  { e.Fit(std::move(erase_map)) };
  { ce.Cost(value, type_id) } -> std::convertible_to<std::size_t>;
  { ce.budget } -> std::convertible_to<typename Eviction::CostType>;
  { ce.consumed } -> std::convertible_to<typename Eviction::CostType>;
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
  using ConstIteratorType = typename ContainerType::const_iterator;
  using CostType = std::size_t;
  using CostFunction
    = std::function<size_t(const std::shared_ptr<void>&, TypeId)>;

  CostType budget;
  CostType consumed { 0 };
  ContainerType eviction_list;

  // Cost estimator: uses user-provided function if set, else returns 1
  [[nodiscard]] auto Cost(
    const std::shared_ptr<void>& value, const TypeId type_id) const -> size_t
  {
    if (cost_fn_) {
      return cost_fn_(value, type_id);
    }
    return 1;
  }

  explicit RefCountedEviction(const CostType _budget, CostFunction cost_fn = {})
    : budget(_budget)
    , cost_fn_(std::move(cost_fn))
  {
    // Allow unlimited budget (no validation needed for max value)
  }

  auto Clear() -> void
  {
    eviction_list.clear();
    consumed = 0;
  }

  auto Store(EntryType&& entry) -> IteratorType
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

  auto Evict(IteratorType& it) -> std::optional<EntryType>
  {
    if (std::get<3>(*it) == 1) {
      auto& value = std::get<2>(*it);
      auto type_id = std::get<1>(*it);
      consumed -= Cost(value, type_id);
      auto evicted_entry = *it; // Save the entry before erasing
      it = eviction_list.erase(it);
      return evicted_entry;
    }
    return std::nullopt;
  }

  auto CheckOut(IteratorType& it) -> void { ++std::get<3>(*it); }

  auto CheckIn(IteratorType& it) -> std::optional<EntryType>
  {
    auto& refcount = std::get<3>(*it);
    if (refcount > 0) {
      --refcount;
      if (refcount == 0) {
        auto& value = std::get<2>(*it);
        auto type_id = std::get<1>(*it);
        consumed -= Cost(value, type_id);
        auto evicted_entry = *it; // Save the entry before erasing
        it = eviction_list.erase(it);
        return evicted_entry;
      }
    }
    return std::nullopt;
  }

  static auto Fit(std::function<void(const K&)> /*erase_map*/) -> void // NOLINT
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

  //! Construct a cache with a given budget.
  explicit AnyCache(typename EvictionPolicyType::CostType budget
    = (std::numeric_limits<typename EvictionPolicyType::CostType>::max)())
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
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    auto type_id = value
      ? std::remove_reference_t<decltype(*value)>::ClassTypeId()
      : kInvalidTypeId;
    std::shared_ptr<void> erased = std::move(value);
    if (it != map_.end()) {
      // Already exists: try replacing
      if (!eviction_.TryReplace(it->second, erased)) {
        return false;
      }
      // Update type id as well
      std::get<1>(*(it->second)) = type_id;
      checkout_state_[key] = 1; // Mark as checked out since caller is using it
      return true;
    }
    EntryType entry = std::make_tuple(
      key, type_id, erased, 0); // eviction_.Store will set refcount
    IteratorType ev_it = eviction_.Store(std::move(entry));
    if (ev_it == eviction_.eviction_list.end()) {
      // Try to fit and retry once
      eviction_.Fit([this](const KeyType& k) { this->map_.erase(k); });
      entry = std::make_tuple(
        key, type_id, erased, 0); // eviction_.Store will set refcount
      ev_it = eviction_.Store(std::move(entry));
      if (ev_it == eviction_.eviction_list.end()) {
        return false;
      }
    }
    map_[key] = ev_it;
    checkout_state_[key] = 1; // Mark as checked out since caller is using it
    return true;
  }

  //! Replace an existing value by key. Returns false if key not present or not
  //! replaceable.
  template <CacheValueType V>
  auto Replace(const KeyType& key, const V& value) -> bool
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
    auto& entry = *(it->second);
    if (eviction_.TryReplace(it->second, erased)) {
      // Call eviction callback for the old value
      if (on_eviction_) {
        on_eviction_(
          std::get<0>(entry), std::get<2>(entry), std::get<1>(entry));
      }
      std::get<1>(entry) = type_id;
      // Assert that the item is already checked out (since TryReplace only
      // succeeds when refcount == 1)
      DCHECK_F(checkout_state_.contains(key) && checkout_state_.at(key) == 1,
        "Item must be checked out with count 1 for Replace to succeed");
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
  template <IsTyped V> auto CheckOut(const KeyType& key) -> std::shared_ptr<V>
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      auto& entry = *(it->second);
      eviction_.CheckOut(it->second);
      ++checkout_state_[key]; // Track checkout at cache level (increment from 0
                              // or existing count)
      TypeId stored_type = std::get<1>(entry);
      if constexpr (requires { V::ClassTypeId(); }) {
        if (stored_type == V::ClassTypeId()) {
          return std::static_pointer_cast<V>(std::get<2>(entry));
        }
      }
    }
    return {};
  }

  //! Mark an item as checked out without returning it.
  /*!
   This method has the same effect as the strongly typed CheckOut method, but
   can be used when you simply need to mark an item as in use without
   retrieving it. This is similar to touching a file to update its stats without
   actually accessing its contents.
  */
  auto Touch(const KeyType& key) -> void
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      eviction_.CheckOut(it->second);
      ++checkout_state_[key]; // Track checkout at cache level (increment from 0
                              // or existing count)
    }
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
      auto& entry = *(it->second);
      TypeId stored_type = std::get<1>(entry);
      if constexpr (requires { V::ClassTypeId(); }) {
        if (stored_type == V::ClassTypeId()) {
          return std::static_pointer_cast<V>(std::get<2>(entry));
        }
      }
    }
    return {};
  }

  //! Check in (return) a previously checked out value.
  auto CheckIn(const KeyType& key) -> void
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      // Update cache-level checkout state
      auto checkout_it = checkout_state_.find(key);
      if (checkout_it != checkout_state_.end() && checkout_it->second > 0) {
        checkout_it->second--;
        if (checkout_it->second == 0) {
          checkout_state_.erase(checkout_it);
        }
      }

      auto evicted_entry = eviction_.CheckIn(it->second);
      if (evicted_entry) {
        // Item was evicted - call callback and remove from map
        if (on_eviction_) {
          on_eviction_(std::get<0>(*evicted_entry), std::get<2>(*evicted_entry),
            std::get<1>(*evicted_entry));
        }
        checkout_state_.erase(key); // Clean up checkout state
        map_.erase(it);
      }
    }
  }

  //! Remove a value by key if permitted by the eviction policy.
  auto Remove(const KeyType& key) -> bool
  {
    std::unique_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      auto evicted_entry = eviction_.Evict(it->second);
      if (evicted_entry) {
        // Item was evicted - call callback and remove from map
        if (on_eviction_) {
          on_eviction_(std::get<0>(*evicted_entry), std::get<2>(*evicted_entry),
            std::get<1>(*evicted_entry));
        }
        checkout_state_.erase(key); // Clean up checkout state
        map_.erase(it);
        return true;
      }
      return false;
    }
    return false;
  }

  //! Remove all items from the cache, ignoring constraints.
  auto Clear() -> void
  {
    std::unique_lock lock(mutex_);
    if (on_eviction_) {
      for (auto& [key, it] : map_) {
        auto& entry = *(it);
        on_eviction_(
          std::get<0>(entry), std::get<2>(entry), std::get<1>(entry));
      }
    }
    eviction_.Clear();
    map_.clear();
    checkout_state_.clear();
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
      return std::get<1>(*(it->second));
    }
    return kInvalidTypeId;
  }

  auto IsCheckedOut(const KeyType& key) const noexcept -> bool
  {
    std::shared_lock lock(mutex_);
    auto checkout_it = checkout_state_.find(key);
    return checkout_it != checkout_state_.end() && checkout_it->second > 0;
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
    auto checkout_it = checkout_state_.find(key);
    return checkout_it != checkout_state_.end() ? checkout_it->second : 0;
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
    const auto& entry = *(it->second);
    return std::get<2>(entry).use_count();
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
  auto Consumed() const noexcept -> typename EvictionPolicyType::CostType
  {
    std::shared_lock lock(mutex_);
    return eviction_.consumed;
  }

  //! Returns the maximum allowed cost (budget) for the cache.
  auto Budget() const noexcept -> typename EvictionPolicyType::CostType
  {
    std::shared_lock lock(mutex_);
    return eviction_.budget;
  }

  //=== Eviction Notification ===---------------------------------------------//

  using EvictionCallbackFunction
    = std::function<void(const KeyType&, std::shared_ptr<void>, TypeId)>;

  class EvictionNotificationScope {
  public:
    EvictionNotificationScope(AnyCache& cache, EvictionCallbackFunction cb)
      : cache_(cache)
      , prev_([&] {
        std::swap(cache.on_eviction_, cb);
        return std::move(cb);
      }())
    {
    }

    ~EvictionNotificationScope() { cache_.on_eviction_ = std::move(prev_); }

    OXYGEN_MAKE_NON_COPYABLE(EvictionNotificationScope);
    OXYGEN_DEFAULT_MOVABLE(EvictionNotificationScope);

  private:
    AnyCache& cache_;
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
  mutable std::shared_mutex mutex_;
  EvictionPolicyType eviction_;
  std::unordered_map<KeyType, IteratorType, Hash> map_;
  std::unordered_map<KeyType, std::size_t, Hash>
    checkout_state_; // Track checkout count per key
  EvictionCallbackFunction on_eviction_;
};

} // namespace oxygen
