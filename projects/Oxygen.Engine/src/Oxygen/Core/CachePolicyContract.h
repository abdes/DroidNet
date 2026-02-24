//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <Oxygen/Composition/TypeSystem.h>

namespace oxygen {

enum class PolicyBudgetStatus : uint8_t {
  kSatisfied,
  kUnsatisfiedBestEffort,
};

[[nodiscard]] constexpr auto to_string(const PolicyBudgetStatus status) noexcept
  -> const char*
{
  switch (status) {
  case PolicyBudgetStatus::kSatisfied:
    return "satisfied";
  case PolicyBudgetStatus::kUnsatisfiedBestEffort:
    return "unsatisfied_best_effort";
  }
  return "__Unknown__";
}

template <typename Eviction, typename K>
concept EvictionPolicyType = requires(Eviction e, const Eviction ce,
  typename Eviction::EntryType entry, typename Eviction::IteratorType it, K key,
  const std::shared_ptr<void>& value, TypeId type_id,
  typename Eviction::CostType budget, std::function<void(const K&)> erase_map) {
  { Eviction(budget) };
  { e.Clear() };
  { e.SetBudget(budget) };
  { ce.Budget() } -> std::convertible_to<typename Eviction::CostType>;
  { ce.Consumed() } -> std::convertible_to<typename Eviction::CostType>;
  {
    ce.MakeEntry(key, type_id, value)
  } -> std::same_as<typename Eviction::EntryType>;
  {
    e.Store(std::move(entry))
  } -> std::same_as<typename Eviction::IteratorType>;
  { ce.IsEnd(it) } -> std::same_as<bool>;
  { e.TryReplace(it, value, type_id) } -> std::same_as<bool>;
  { e.Evict(it) } -> std::same_as<std::optional<typename Eviction::EntryType>>;
  {
    e.CheckIn(it)
  } -> std::same_as<std::optional<typename Eviction::EntryType>>;
  { e.CheckOut(it) };
  { e.EnforceBudget(std::move(erase_map)) } -> std::same_as<PolicyBudgetStatus>;
  { ce.Cost(value, type_id) } -> std::convertible_to<std::size_t>;
  { ce.KeyOf(it) } -> std::convertible_to<const K&>;
  { ce.TypeOf(it) } -> std::convertible_to<TypeId>;
  { ce.ValueOf(it) } -> std::same_as<const std::shared_ptr<void>&>;
  { ce.RefCountOf(it) } -> std::convertible_to<std::size_t>;
  { ce.EntryKey(entry) } -> std::convertible_to<const K&>;
  { ce.EntryTypeId(entry) } -> std::convertible_to<TypeId>;
  { ce.EntryValue(entry) } -> std::same_as<const std::shared_ptr<void>&>;
};

} // namespace oxygen
