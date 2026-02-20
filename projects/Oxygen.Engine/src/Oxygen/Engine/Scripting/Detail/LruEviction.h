//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <tuple>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>

namespace oxygen::scripting::detail {

//! Generic Least-Recently-Used (LRU) eviction policy for use with AnyCache.
/*!
  LruEviction maintains a list of cached items ordered by their last access
  time. It supports budget-aware eviction based on a pluggable cost function.

  ### Design Contracts
  - **Budget-Aware**: Decision to evict is driven by 'budget' vs 'consumed'
  cost.
  - **Pluggable Cost**: Accepts a lambda to calculate the weight of an item
    (e.g., its memory size in bytes).
  - **Reference Counted**: Only items with a reference count of 1 (held only by
    the cache) are eligible for background eviction.
*/
template <typename K> struct LruEviction {
  using KeyType = K;
  using ValueType = std::shared_ptr<void>;
  using RefcountType = std::size_t;
  using EntryType = std::tuple<KeyType, TypeId, ValueType, RefcountType>;
  using ContainerType = std::list<EntryType>;
  using IteratorType = typename ContainerType::iterator;
  using CostType = std::size_t;
  using CostFunc = std::function<CostType(const ValueType&, const TypeId)>;

  CostType budget;
  CostType consumed { 0 };
  ContainerType eviction_list;
  CostFunc cost_func;

  LruEviction() = default;

  explicit LruEviction(const CostType _budget, CostFunc _cost_func = nullptr)
    : budget(_budget)
    , cost_func(std::move(_cost_func))
  {
  }

  auto Clear() -> void
  {
    eviction_list.clear();
    consumed = 0;
  }

  [[nodiscard]] auto Cost(
    const std::shared_ptr<void>& value, const TypeId type_id) const -> size_t
  {
    if (cost_func) {
      return cost_func(value, type_id);
    }
    return 1;
  }

  auto Store(EntryType&& entry) -> IteratorType
  {
    std::get<3>(entry) = 1;
    const auto item_cost = Cost(std::get<2>(entry), std::get<1>(entry));
    if (consumed + item_cost > budget) {
      return eviction_list.end();
    }
    eviction_list.emplace_front(std::move(entry));
    consumed += item_cost;
    return eviction_list.begin();
  }

  auto TryReplace(IteratorType& it, const std::shared_ptr<void>& value) -> bool
  {
    if (std::get<3>(*it) != 1) {
      return false;
    }
    const auto type_id = std::get<1>(*it);
    const auto old_cost = Cost(std::get<2>(*it), type_id);
    const auto new_cost = Cost(value, type_id);
    if ((consumed - old_cost + new_cost) > budget) {
      return false;
    }
    consumed = consumed - old_cost + new_cost;
    std::get<2>(*it) = value;
    eviction_list.splice(eviction_list.begin(), eviction_list, it);
    return true;
  }

  auto Evict(IteratorType& it) -> std::optional<EntryType>
  {
    if (std::get<3>(*it) != 1) {
      return std::nullopt;
    }
    consumed -= Cost(std::get<2>(*it), std::get<1>(*it));
    auto evicted = *it;
    it = eviction_list.erase(it);
    return evicted;
  }

  auto CheckOut(IteratorType& it) -> void
  {
    ++std::get<3>(*it);
    eviction_list.splice(eviction_list.begin(), eviction_list, it);
  }

  auto CheckIn(IteratorType& it) -> std::optional<EntryType>
  {
    auto& refcount = std::get<3>(*it);
    if (refcount == 0) {
      return std::nullopt;
    }
    --refcount;
    if (refcount == 0) {
      consumed -= Cost(std::get<2>(*it), std::get<1>(*it));
      auto evicted = *it;
      it = eviction_list.erase(it);
      return evicted;
    }
    eviction_list.splice(eviction_list.begin(), eviction_list, it);
    return std::nullopt;
  }

  auto Fit(std::function<void(const K&)> erase_map) -> void
  {
    auto evicted_at_least_one = false;
    while (true) {
      auto evicted_this_pass = false;
      for (auto it = eviction_list.rbegin(); it != eviction_list.rend();) {
        auto base_it = std::prev(it.base());
        if (std::get<3>(*base_it) == 1) {
          const auto key = std::get<0>(*base_it);
          consumed -= Cost(std::get<2>(*base_it), std::get<1>(*base_it));
          it = std::make_reverse_iterator(eviction_list.erase(base_it));
          erase_map(key);
          evicted_this_pass = true;
          evicted_at_least_one = true;
          break;
        }
        ++it;
      }
      if (!evicted_this_pass) {
        break;
      }
      if (evicted_at_least_one && consumed <= budget) {
        break;
      }
    }
  }
};

} // namespace oxygen::scripting::detail
