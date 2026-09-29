//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
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

//! Thread-safe heterogeneous cache with exact usage tickets.
//! Acquire/StoreAndAcquire return a typed payload and one move-only ticket.
//! Return the ticket through Release; payload pointer copies do not add usages.
//! The owning service supplies RAII and any required deferred return policy.
//! Peek keeps CPU storage alive without reserving residency. Invalidation
//! retires the exact publication; old tickets never affect a replacement under
//! its key. Keys() requires external synchronization; KeysSnapshot() is
//! thread-safe.
template <typename Key, typename Evict, typename Hash = std::hash<Key>>
  requires EvictionPolicyType<Evict, Key>
class AnyCache {
public:
  using KeyType = Key;
  using EvictionPolicyType = Evict;
  using EntryType = EvictionPolicyType::EntryType;
  using IteratorType = EvictionPolicyType::IteratorType;
  using CostType = EvictionPolicyType::CostType;

  enum class UsageKind : uint8_t { kCheckout, kPin };

  //! Identifies one payload incarnation, independently of its lookup key.
  class EntryReference final {
  public:
    ~EntryReference() = default;
    OXYGEN_DEFAULT_COPYABLE(EntryReference)
    OXYGEN_DEFAULT_MOVABLE(EntryReference)

    [[nodiscard]] auto GetKey() const noexcept -> const KeyType&
    {
      return key_;
    }
    auto operator==(const EntryReference&) const -> bool = default;

  private:
    friend class AnyCache;
    EntryReference(const KeyType& key, Uuid cache, const uint64_t incarnation)
      : key_(key)
      , cache_(cache)
      , incarnation_(incarnation)
    {
    }

    KeyType key_;
    Uuid cache_ {};
    uint64_t incarnation_ = 0;
  };

  //! A single cache usage. The owning service decides when to return it.
  class UsageTicket final {
  public:
    static_assert(std::is_nothrow_move_constructible_v<KeyType>,
      "Cache usage tickets require non-throwing key moves");
    ~UsageTicket() = default;
    OXYGEN_MAKE_NON_COPYABLE(UsageTicket)

    UsageTicket(UsageTicket&& other) noexcept(
      std::is_nothrow_move_constructible_v<KeyType>)
      : entry_(std::move(other.entry_))
      , owner_(other.owner_)
      , kind_(other.kind_)
      , active_(std::exchange(other.active_, false))
    {
    }
    auto operator=(UsageTicket&&) -> UsageTicket& = delete;

    [[nodiscard]] auto Entry() const noexcept -> const EntryReference&
    {
      return entry_;
    }
    [[nodiscard]] auto IsActive() const noexcept -> bool { return active_; }

  private:
    friend class AnyCache;
    UsageTicket(EntryReference entry, CheckoutOwner owner, UsageKind kind)
      : entry_(std::move(entry))
      , owner_(owner)
      , kind_(kind)
    {
    }

    EntryReference entry_;
    CheckoutOwner owner_ { CheckoutOwner::kInternal };
    UsageKind kind_ { UsageKind::kCheckout };
    bool active_ = true;
  };

  template <IsTyped V> struct Acquisition final {
    std::shared_ptr<V> value {};
    UsageTicket ticket;
  };

  //! Owns the exact retired publication until notification finishes.
  struct RetiredEntry final {
    EntryReference entry;
    std::shared_ptr<void> value {};
    TypeId type { kInvalidTypeId };
  };

  //! Acquire one exact, typed usage of the current cache entry.
  template <IsTyped V>
  [[nodiscard]] auto Acquire(const KeyType& key, CheckoutOwner owner,
    const UsageKind kind = UsageKind::kCheckout)
    -> std::optional<Acquisition<V>>
  {
    std::unique_lock lock(mutex_);
    const auto found = map_.find(key);
    if (found == map_.end()) {
      return std::nullopt;
    }
    return AcquireRecord<V>(found->first, found->second, owner, kind);
  }

  //! Acquire only the publication represented by this reference.
  template <IsTyped V>
  [[nodiscard]] auto Acquire(const EntryReference& entry, CheckoutOwner owner)
    -> std::optional<Acquisition<V>>
  {
    std::unique_lock lock(mutex_);
    const auto found = map_.find(entry.key_);
    if (entry.cache_ != *identity_ || found == map_.end()
      || found->second.incarnation != entry.incarnation_) {
      return std::nullopt;
    }
    return AcquireRecord<V>(
      found->first, found->second, owner, UsageKind::kCheckout);
  }

  [[nodiscard]] auto AcquirePin(const KeyType& key, CheckoutOwner owner)
    -> std::optional<UsageTicket>
  {
    std::unique_lock lock(mutex_);
    const auto found = map_.find(key);
    if (found == map_.end()) {
      return std::nullopt;
    }
    UsageTicket ticket { EntryReference { found->first, *identity_,
                           found->second.incarnation },
      owner, UsageKind::kPin };
    AddUsage(found->second, owner, UsageKind::kPin);
    return ticket;
  }

  //! Return exactly this usage; an old incarnation cannot debit a new entry.
  auto Release(UsageTicket&& ticket) -> bool
  {
    std::unique_lock lock(mutex_);
    if (!ticket.active_ || ticket.entry_.cache_ != *identity_) {
      return false;
    }
    const auto found = map_.find(ticket.entry_.key_);
    if (found == map_.end()
      || found->second.incarnation != ticket.entry_.incarnation_) {
      ticket.active_ = false;
      return false;
    }
    auto& count = UsageCount(found->second.owners, ticket.owner_, ticket.kind_);
    if (count == 0) {
      ticket.active_ = false;
      return false;
    }
    const auto callback = on_eviction_;
    auto evicted = eviction_.CheckIn(found->second.policy);
    --count;
    ticket.active_ = false;
    if (evicted) {
      RetiredEntry retired { .entry = std::move(ticket.entry_),
        .value = eviction_.EntryValue(*evicted),
        .type = eviction_.EntryTypeId(*evicted) };
      map_.erase(found);
      lock.unlock();
      Rethrow(
        DispatchEvictions(callback, std::span { &retired, 1U }, identity_));
    }
    return true;
  }

  template <IsTyped V>
  [[nodiscard]] auto Contains(const KeyType& key) const -> bool
  {
    std::shared_lock lock(mutex_);
    const auto found = map_.find(key);
    return found != map_.end()
      && eviction_.TypeOf(found->second.policy) == V::ClassTypeId();
  }

  [[nodiscard]] auto Contains(const EntryReference& entry) const -> bool
  {
    std::shared_lock lock(mutex_);
    const auto found = map_.find(entry.key_);
    return entry.cache_ == *identity_ && found != map_.end()
      && found->second.incarnation == entry.incarnation_;
  }

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
  explicit AnyCache(CostType budget = std::numeric_limits<CostType>::max())
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

    @see Replace, Acquire, EvictionPolicyType
  */
  template <CacheValueType V> auto Store(const KeyType& key, V value) -> bool
  {
    using Value = typename V::element_type;
    const auto type = value ? Value::ClassTypeId() : kInvalidTypeId;
    return StoreValue(key, std::move(value), type, std::nullopt).stored;
  }

  //! Publish with a usage already installed before eviction callbacks run.
  template <IsTyped V>
  [[nodiscard]] auto StoreAndAcquire(const KeyType& key,
    std::shared_ptr<V> value, const CheckoutOwner owner,
    const UsageKind kind = UsageKind::kCheckout)
    -> std::optional<Acquisition<V>>
  {
    if (!value) {
      return std::nullopt;
    }
    auto result
      = StoreValue(key, value, V::ClassTypeId(), UsageRequest { owner, kind });
    if (!result.usage) {
      return std::nullopt;
    }
    return Acquisition<V> { .value = std::move(value),
      .ticket = std::move(*result.usage) };
  }

  //! Replace an existing value by key. Returns false if key not present or not
  //! replaceable.
  template <CacheValueType V>
  auto Replace(const KeyType& key, const V& value) -> bool
  {
    std::unique_lock lock(mutex_);
    const auto found = map_.find(key);
    if (found == map_.end()) {
      return false;
    }
    const auto retired = SnapshotRetired(found->first, found->second);
    const auto callback = on_eviction_;
    const auto incarnation = AllocateIncarnation();
    using Value = typename V::element_type;
    const auto type = value ? Value::ClassTypeId() : kInvalidTypeId;
    if (!eviction_.TryReplace(found->second.policy, value, type)) {
      return false;
    }
    found->second.incarnation = incarnation;
    found->second.owners = {};
    lock.unlock();
    Rethrow(DispatchEvictions(callback, std::span { &retired, 1U }, identity_));
    return true;
  }

  /*!
    Peek at a value by key without affecting usage state.

    @tparam V Value type (must satisfy IsTyped concept).
    @param key The key to peek.
    @return Shared pointer to the value if present and type matches, else empty.

    @see Acquire
  */
  template <IsTyped V> auto Peek(const KeyType& key) const -> std::shared_ptr<V>
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      TypeId stored_type = eviction_.TypeOf(it->second.policy);
      if constexpr (requires { V::ClassTypeId(); }) {
        if (stored_type == V::ClassTypeId()) {
          return std::static_pointer_cast<V>(
            eviction_.ValueOf(it->second.policy));
        }
      }
    }
    return {};
  }

  //! Remove a value by key if permitted by the eviction policy.
  auto Remove(const KeyType& key) -> bool
  {
    std::unique_lock lock(mutex_);
    const auto found = map_.find(key);
    if (found == map_.end()) {
      return false;
    }
    const auto retired = SnapshotRetired(found->first, found->second);
    const auto callback = on_eviction_;
    if (!eviction_.Evict(found->second.policy)) {
      return false;
    }
    map_.erase(found);
    lock.unlock();
    Rethrow(DispatchEvictions(callback, std::span { &retired, 1U }, identity_));
    return true;
  }

  //! Remove all entries; outstanding tickets become stale.
  auto Invalidate(const KeyType& key) -> bool
  {
    std::unique_lock lock(mutex_);
    const auto found = map_.find(key);
    if (found == map_.end()) {
      return false;
    }
    const auto retired = SnapshotRetired(found->first, found->second);
    const auto callback = on_eviction_;
    static_cast<void>(eviction_.Erase(found->second.policy));
    map_.erase(found);
    lock.unlock();
    Rethrow(DispatchEvictions(callback, std::span { &retired, 1U }, identity_));
    return true;
  }

  //! Remove all entries; outstanding tickets become stale.
  auto Clear() -> void
  {
    std::vector<RetiredEntry> retired;
    std::unique_lock lock(mutex_);
    const auto callback = on_eviction_;
    retired.reserve(map_.size());
    for (const auto& [key, record] : map_) {
      retired.push_back(SnapshotRetired(key, record));
    }
    eviction_.Clear();
    map_.clear();
    lock.unlock();
    Rethrow(DispatchEvictions(callback, retired, identity_));
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
      return eviction_.TypeOf(it->second.policy);
    }
    return kInvalidTypeId;
  }

  auto IsCheckedOut(const KeyType& key) const noexcept -> bool
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    return it != map_.end() && eviction_.RefCountOf(it->second.policy) > 0;
  }

  //! Policy reference count, including its single cache-residency reference.
  auto GetCheckoutCount(const KeyType& key) const noexcept -> std::size_t
  {
    std::shared_lock lock(mutex_);
    auto it = map_.find(key);
    return it != map_.end() ? eviction_.RefCountOf(it->second.policy) : 0;
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
    return eviction_.ValueOf(it->second.policy).use_count();
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
      if (eviction_.RefCountOf(it.policy) > 0) {
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
      static_cast<void>(key);
      {
        const auto& counts = it.owners;
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
      const auto refs = eviction_.RefCountOf(it.policy);
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
    std::vector<RetiredEntry> retired;
    std::exception_ptr failure;
    auto status = PolicyBudgetStatus::kUnsatisfiedBestEffort;
    std::unique_lock lock(mutex_);
    const auto callback = on_eviction_;
    try {
      eviction_.SetBudget(budget);
      status = EnforceBudget(retired);
    } catch (...) {
      failure = std::current_exception();
    }
    lock.unlock();
    Rethrow(DispatchEvictions(callback, retired, identity_, failure));
    return status;
  }

  [[nodiscard]] auto GetPolicy() noexcept -> Evict& { return eviction_; }
  [[nodiscard]] auto GetPolicy() const noexcept -> const Evict&
  {
    return eviction_;
  }

  //=== Eviction Notification ===---------------------------------------------//

  using EvictionCallbackFunction = std::function<void(const RetiredEntry&)>;

  class EvictionNotificationScope {
  public:
    EvictionNotificationScope(
      AnyCache& cache, EvictionCallbackFunction callback)
      : cache_(&cache)
      , identity_(cache.identity_)
      , previous_(cache.ExchangeEvictionCallback(
          std::make_shared<EvictionCallbackFunction>(std::move(callback))))
    {
    }

    ~EvictionNotificationScope()
    {
      if (cache_ && !identity_.expired()) {
        static_cast<void>(
          cache_->ExchangeEvictionCallback(std::move(previous_)));
      }
    }

    OXYGEN_MAKE_NON_COPYABLE(EvictionNotificationScope)
    EvictionNotificationScope(EvictionNotificationScope&& other) noexcept
      : cache_(std::exchange(other.cache_, nullptr))
      , identity_(std::move(other.identity_))
      , previous_(std::move(other.previous_))
    {
    }
    auto operator=(EvictionNotificationScope&&)
      -> EvictionNotificationScope& = delete;

  private:
    observer_ptr<AnyCache> cache_;
    std::weak_ptr<const Uuid> identity_;
    std::shared_ptr<EvictionCallbackFunction> previous_ {};
  };

  //! Scopes nest on the registering thread; callbacks execute outside the cache
  //! lock. All committed retirements are delivered before the first callback
  //! error propagates.
  [[nodiscard]] auto OnEviction(EvictionCallbackFunction callback)
    -> EvictionNotificationScope
  {
    return EvictionNotificationScope(*this, std::move(callback));
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
      using base_iter = MapType::const_iterator;
      base_iter it_;

    public:
      using iterator_concept = std::forward_iterator_tag;
      using value_type = MapType::key_type;
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
         return cache.Peek<MyType>(key);
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
  auto Keys() const { return KeysView<decltype(map_)>(map_); }

private:
  auto ExchangeEvictionCallback(
    std::shared_ptr<EvictionCallbackFunction> callback)
    -> std::shared_ptr<EvictionCallbackFunction>
  {
    std::unique_lock lock(mutex_);
    return std::exchange(on_eviction_, std::move(callback));
  }

  static auto DispatchEvictions(
    const std::shared_ptr<EvictionCallbackFunction>& callback,
    const std::span<const RetiredEntry> retired,
    const std::weak_ptr<const Uuid>& lifetime,
    std::exception_ptr failure = {}) noexcept -> std::exception_ptr
  {
    if (callback && *callback) {
      for (const auto& entry : retired) {
        if (lifetime.expired()) {
          break;
        }
        try {
          (*callback)(entry);
        } catch (...) {
          if (!failure) {
            failure = std::current_exception();
          }
        }
      }
    }
    return failure;
  }

  static auto Rethrow(const std::exception_ptr& failure) -> void
  {
    if (failure) {
      std::rethrow_exception(failure);
    }
  }

  mutable std::shared_mutex mutex_;
  EvictionPolicyType eviction_;
  struct OwnerCounts final {
    uint32_t checkout_internal { 0 };
    uint32_t checkout_external { 0 };
    uint32_t pin_internal { 0 };
    uint32_t pin_external { 0 };
  };
  struct Record final {
    IteratorType policy {};
    uint64_t incarnation = 0;
    OwnerCounts owners {};
  };

  auto SnapshotRetired(const KeyType& key, const Record& record) const
    -> RetiredEntry
  {
    return { .entry = EntryReference { key, *identity_, record.incarnation },
      .value = eviction_.ValueOf(record.policy),
      .type = eviction_.TypeOf(record.policy) };
  }

  auto EnforceBudget(std::vector<RetiredEntry>& retired) -> PolicyBudgetStatus
  {
    return eviction_.EnforceBudget([&](const KeyType& key) {
      const auto found = map_.find(key);
      if (found != map_.end()) {
        // Snapshot construction precedes the policy's destructive step.
        retired.push_back(SnapshotRetired(found->first, found->second));
        map_.erase(found);
      }
    });
  }

  static auto UsageCount(OwnerCounts& counts, const CheckoutOwner owner,
    const UsageKind kind) -> uint32_t&
  {
    switch (owner) {
    case CheckoutOwner::kInternal:
      return kind == UsageKind::kPin ? counts.pin_internal
                                     : counts.checkout_internal;
    case CheckoutOwner::kExternal:
      return kind == UsageKind::kPin ? counts.pin_external
                                     : counts.checkout_external;
    }
    throw std::invalid_argument("Unknown cache usage owner");
  }

  auto AddUsage(Record& record, const CheckoutOwner owner, const UsageKind kind)
    -> void
  {
    auto& count = UsageCount(record.owners, owner, kind);
    if (count == std::numeric_limits<uint32_t>::max()
      || eviction_.RefCountOf(record.policy)
        == std::numeric_limits<std::size_t>::max()) {
      throw std::overflow_error("Cache usage count exhausted");
    }
    eviction_.CheckOut(record.policy);
    ++count;
  }

  template <IsTyped V>
  auto AcquireRecord(const KeyType& key, Record& record,
    const CheckoutOwner owner, const UsageKind kind)
    -> std::optional<Acquisition<V>>
  {
    if (eviction_.TypeOf(record.policy) != V::ClassTypeId()) {
      return std::nullopt;
    }
    Acquisition<V> result {
      .value = std::static_pointer_cast<V>(eviction_.ValueOf(record.policy)),
      .ticket
      = UsageTicket { EntryReference { key, *identity_, record.incarnation },
        owner, kind },
    };
    AddUsage(record, owner, kind);
    return result;
  }

  struct UsageRequest final {
    CheckoutOwner owner {};
    UsageKind kind {};
  };
  struct StoreResult final {
    bool stored = false;
    std::optional<UsageTicket> usage {};
  };

  auto StoreValue(const KeyType& key, std::shared_ptr<void> value,
    const TypeId type, const std::optional<UsageRequest> usage) -> StoreResult
  {
    std::vector<RetiredEntry> retired;
    StoreResult result;
    std::exception_ptr failure;
    std::unique_lock lock(mutex_);
    const auto callback = on_eviction_;
    try {
      Admit(key, value, type, usage, retired, result);
    } catch (...) {
      failure = std::current_exception();
    }
    lock.unlock();
    const std::weak_ptr<const Uuid> alive = identity_;
    failure = DispatchEvictions(callback, retired, identity_, failure);
    if (failure) {
      if (!alive.expired() && result.stored && result.usage) {
        static_cast<void>(Release(std::move(*result.usage)));
      }
      Rethrow(failure);
    }
    return result;
  }

  auto Admit(const KeyType& key, const std::shared_ptr<void>& value,
    const TypeId type, const std::optional<UsageRequest> usage,
    std::vector<RetiredEntry>& retired, StoreResult& result) -> void
  {
    const auto incarnation = AllocateIncarnation();
    std::optional<UsageTicket> ticket;
    if (usage) {
      OwnerCounts validation {};
      static_cast<void>(UsageCount(validation, usage->owner, usage->kind));
      ticket.emplace(
        UsageTicket { EntryReference { key, *identity_, incarnation },
          usage->owner, usage->kind });
    }
    const auto [position, inserted] = map_.try_emplace(key);
    bool admitted = false;
    const auto rollback = Finally([&]() noexcept {
      if (inserted && !admitted) {
        map_.erase(position);
      }
    });
    if (!inserted) {
      retired.push_back(SnapshotRetired(position->first, position->second));
      bool replaced = false;
      const auto discard_failed = Finally([&]() noexcept {
        if (!replaced) {
          retired.pop_back();
        }
      });
      replaced = eviction_.TryReplace(position->second.policy, value, type);
      if (!replaced) {
        return;
      }
    } else {
      auto policy_entry
        = eviction_.Store(eviction_.MakeEntry(key, type, value));
      if (eviction_.IsEnd(policy_entry)) {
        const auto cost = eviction_.Cost(value, type);
        const auto budget = eviction_.Budget();
        if (cost > std::numeric_limits<CostType>::max()
          || static_cast<CostType>(cost) > budget) {
          return;
        }
        {
          const auto restore_budget
            = Finally([&]() noexcept { eviction_.SetBudget(budget); });
          eviction_.SetBudget(budget - static_cast<CostType>(cost));
          static_cast<void>(EnforceBudget(retired));
        }
        policy_entry = eviction_.Store(eviction_.MakeEntry(key, type, value));
        if (eviction_.IsEnd(policy_entry)) {
          return;
        }
      }
      position->second.policy = policy_entry;
    }
    position->second.incarnation = incarnation;
    position->second.owners = {};
    if (usage) {
      AddUsage(position->second, usage->owner, usage->kind);
      result.usage.emplace(std::move(*ticket));
    }
    admitted = true;
    result.stored = true;
  }

  std::unordered_map<KeyType, Record, Hash> map_;
  std::shared_ptr<const Uuid> identity_ { std::make_shared<const Uuid>(
    Uuid::Generate()) };
  uint64_t next_incarnation_ = 1;

  auto AllocateIncarnation() -> uint64_t
  {
    if (next_incarnation_ == std::numeric_limits<uint64_t>::max()) {
      throw std::overflow_error("Cache entry incarnations exhausted");
    }
    return next_incarnation_++;
  }
  std::shared_ptr<EvictionCallbackFunction> on_eviction_ {};
};

} // namespace oxygen
