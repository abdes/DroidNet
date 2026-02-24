//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <tuple>

#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Core/CachePolicyContract.h>

namespace oxygen {

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

  [[nodiscard]] auto Cost(
    const std::shared_ptr<void>& value, const TypeId type_id) const -> size_t
  {
    if (cost_fn_) {
      return cost_fn_(value, type_id);
    }
    return 1;
  }

  explicit RefCountedEviction(const CostType budget, CostFunction cost_fn = {})
    : budget_(budget)
    , cost_fn_(std::move(cost_fn))
  {
  }

  auto SetBudget(const CostType budget) -> void { budget_ = budget; }
  [[nodiscard]] auto Budget() const noexcept -> CostType { return budget_; }
  [[nodiscard]] auto Consumed() const noexcept -> CostType { return consumed_; }

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

  auto Store(EntryType&& entry) -> IteratorType
  {
    std::get<3>(entry) = 1;
    auto& value = std::get<2>(entry);
    auto type_id = std::get<1>(entry);
    const CostType item_cost = Cost(value, type_id);
    if ((budget_ != 0U) && consumed_ + item_cost > budget_) {
      return eviction_list_.end();
    }
    eviction_list_.emplace_front(std::move(entry));
    consumed_ += item_cost;
    return eviction_list_.begin();
  }

  auto Evict(IteratorType& it) -> std::optional<EntryType>
  {
    if (std::get<3>(*it) == 1) {
      auto& value = std::get<2>(*it);
      auto type_id = std::get<1>(*it);
      consumed_ -= Cost(value, type_id);
      auto evicted_entry = *it;
      it = eviction_list_.erase(it);
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
        consumed_ -= Cost(value, type_id);
        auto evicted_entry = *it;
        it = eviction_list_.erase(it);
        return evicted_entry;
      }
    }
    return std::nullopt;
  }

  auto EnforceBudget(std::function<void(const K&)> /*erase_map*/)
    -> PolicyBudgetStatus
  {
    return PolicyBudgetStatus::kUnsatisfiedBestEffort;
  }

  auto TryReplace(IteratorType& it, const std::shared_ptr<void>& value,
    const TypeId type_id) -> bool
  {
    if (std::get<3>(*it) == 1) {
      const auto old_cost = Cost(std::get<2>(*it), std::get<1>(*it));
      const auto new_cost = Cost(value, type_id);
      if ((budget_ != 0) && (consumed_ - old_cost + new_cost > budget_)) {
        return false;
      }
      consumed_ = consumed_ - old_cost + new_cost;
      std::get<1>(*it) = type_id;
      std::get<2>(*it) = value;
      return true;
    }
    return false;
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
  CostType budget_ {};
  CostType consumed_ { 0 };
  ContainerType eviction_list_;
  CostFunction cost_fn_;
};

} // namespace oxygen
