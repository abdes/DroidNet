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
#include <Oxygen/Core/CachePolicyContract.h>

namespace oxygen {

template <typename K> struct LruEviction {
  using KeyType = K;
  using ValueType = std::shared_ptr<void>;
  using RefcountType = std::size_t;
  using EntryType = std::tuple<KeyType, TypeId, ValueType, RefcountType>;
  using ContainerType = std::list<EntryType>;
  using IteratorType = typename ContainerType::iterator;
  using CostType = std::size_t;
  using CostFunc = std::function<CostType(const ValueType&, const TypeId)>;

  LruEviction() = default;

  explicit LruEviction(const CostType budget, CostFunc cost_func = nullptr)
    : budget_(budget)
    , cost_func_(std::move(cost_func))
  {
  }

  auto SetBudget(const CostType budget) -> void { budget_ = budget; }
  [[nodiscard]] auto Budget() const noexcept -> CostType { return budget_; }
  [[nodiscard]] auto Consumed() const noexcept -> CostType { return consumed_; }
  auto SetCostFunction(CostFunc fn) -> void { cost_func_ = std::move(fn); }

  auto Clear() -> void
  {
    eviction_list_.clear();
    consumed_ = 0;
  }

  auto MakeEntry(const KeyType& key, const TypeId type_id,
    const ValueType& value) const -> EntryType
  {
    return std::make_tuple(key, type_id, value, 0);
  }

  [[nodiscard]] auto IsEnd(const IteratorType& it) const noexcept -> bool
  {
    return it == eviction_list_.end();
  }

  [[nodiscard]] auto Cost(
    const std::shared_ptr<void>& value, const TypeId type_id) const -> size_t
  {
    if (cost_func_) {
      return cost_func_(value, type_id);
    }
    return 1;
  }

  auto Store(EntryType&& entry) -> IteratorType
  {
    std::get<3>(entry) = 1;
    const auto item_cost = Cost(std::get<2>(entry), std::get<1>(entry));
    if (consumed_ + item_cost > budget_) {
      return eviction_list_.end();
    }
    eviction_list_.emplace_front(std::move(entry));
    consumed_ += item_cost;
    return eviction_list_.begin();
  }

  auto TryReplace(IteratorType& it, const std::shared_ptr<void>& value,
    const TypeId type_id) -> bool
  {
    if (std::get<3>(*it) != 1) {
      return false;
    }
    const auto old_cost = Cost(std::get<2>(*it), std::get<1>(*it));
    const auto new_cost = Cost(value, type_id);
    if ((consumed_ - old_cost + new_cost) > budget_) {
      return false;
    }
    consumed_ = consumed_ - old_cost + new_cost;
    std::get<1>(*it) = type_id;
    std::get<2>(*it) = value;
    eviction_list_.splice(eviction_list_.begin(), eviction_list_, it);
    return true;
  }

  auto Evict(IteratorType& it) -> std::optional<EntryType>
  {
    if (std::get<3>(*it) != 1) {
      return std::nullopt;
    }
    consumed_ -= Cost(std::get<2>(*it), std::get<1>(*it));
    auto evicted = *it;
    it = eviction_list_.erase(it);
    return evicted;
  }

  auto CheckOut(IteratorType& it) -> void
  {
    ++std::get<3>(*it);
    eviction_list_.splice(eviction_list_.begin(), eviction_list_, it);
  }

  auto CheckIn(IteratorType& it) -> std::optional<EntryType>
  {
    auto& refcount = std::get<3>(*it);
    if (refcount == 0) {
      return std::nullopt;
    }
    --refcount;
    if (refcount == 0) {
      consumed_ -= Cost(std::get<2>(*it), std::get<1>(*it));
      auto evicted = *it;
      it = eviction_list_.erase(it);
      return evicted;
    }
    eviction_list_.splice(eviction_list_.begin(), eviction_list_, it);
    return std::nullopt;
  }

  auto EnforceBudget(std::function<void(const K&)> erase_map)
    -> PolicyBudgetStatus
  {
    while (consumed_ > budget_) {
      auto evicted_this_pass = false;
      for (auto it = eviction_list_.rbegin(); it != eviction_list_.rend();) {
        auto base_it = std::prev(it.base());
        if (std::get<3>(*base_it) == 1) {
          const auto key = std::get<0>(*base_it);
          erase_map(key);
          consumed_ -= Cost(std::get<2>(*base_it), std::get<1>(*base_it));
          it = std::make_reverse_iterator(eviction_list_.erase(base_it));
          evicted_this_pass = true;
          break;
        }
        ++it;
      }
      if (!evicted_this_pass) {
        break;
      }
    }
    return (consumed_ <= budget_) ? PolicyBudgetStatus::kSatisfied
                                  : PolicyBudgetStatus::kUnsatisfiedBestEffort;
  }

  [[nodiscard]] auto KeyOf(const IteratorType& it) const -> const KeyType&
  {
    return std::get<0>(*it);
  }
  [[nodiscard]] auto TypeOf(const IteratorType& it) const -> TypeId
  {
    return std::get<1>(*it);
  }
  [[nodiscard]] auto ValueOf(const IteratorType& it) const
    -> const std::shared_ptr<void>&
  {
    return std::get<2>(*it);
  }
  [[nodiscard]] auto RefCountOf(const IteratorType& it) const -> std::size_t
  {
    return std::get<3>(*it);
  }
  [[nodiscard]] auto EntryKey(const EntryType& entry) const -> const KeyType&
  {
    return std::get<0>(entry);
  }
  [[nodiscard]] auto EntryTypeId(const EntryType& entry) const -> TypeId
  {
    return std::get<1>(entry);
  }
  [[nodiscard]] auto EntryValue(const EntryType& entry) const
    -> const std::shared_ptr<void>&
  {
    return std::get<2>(entry);
  }

private:
  CostType budget_ { 0 };
  CostType consumed_ { 0 };
  ContainerType eviction_list_;
  CostFunc cost_func_;
};

} // namespace oxygen
