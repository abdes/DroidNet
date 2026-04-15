//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Console/CVar.h>

namespace oxygen::console {

class ConsoleStartupPlan {
public:
  struct Entry {
    std::string name;
    StampedCVarValue stamped_value;
  };

  ConsoleStartupPlan() = default;

  auto Set(std::string name, CVarValue value,
    const CVarValueOrigin origin = CVarValueOrigin::kStartupExplicit)
    -> ConsoleStartupPlan&
  {
    return SetStamped(std::move(name),
      StampedCVarValue {
        .value = std::move(value),
        .origin = origin,
      });
  }

  auto Set(std::string name, std::string_view value,
    const CVarValueOrigin origin = CVarValueOrigin::kStartupExplicit)
    -> ConsoleStartupPlan&
  {
    return Set(std::move(name), CVarValue { std::string(value) }, origin);
  }

  auto Set(std::string name, const char* value,
    const CVarValueOrigin origin = CVarValueOrigin::kStartupExplicit)
    -> ConsoleStartupPlan&
  {
    return Set(std::move(name),
      CVarValue { std::string(value == nullptr ? "" : value) }, origin);
  }

  template <typename TValue>
    requires(std::integral<std::remove_cvref_t<TValue>>
      && !std::same_as<std::remove_cvref_t<TValue>, bool>)
  auto Set(std::string name, TValue value,
    const CVarValueOrigin origin = CVarValueOrigin::kStartupExplicit)
    -> ConsoleStartupPlan&
  {
    return Set(std::move(name), CVarValue { int64_t { value } }, origin);
  }

  template <typename TValue>
    requires(std::floating_point<std::remove_cvref_t<TValue>>)
  auto Set(std::string name, TValue value,
    const CVarValueOrigin origin = CVarValueOrigin::kStartupExplicit)
    -> ConsoleStartupPlan&
  {
    return Set(std::move(name), CVarValue { double { value } }, origin);
  }

  auto SetStamped(std::string name, StampedCVarValue value)
    -> ConsoleStartupPlan&
  {
    if (const auto it = std::ranges::find(entries_, name, &Entry::name);
      it != entries_.end()) {
      it->stamped_value = std::move(value);
    } else {
      entries_.push_back(Entry {
        .name = std::move(name),
        .stamped_value = std::move(value),
      });
    }
    return *this;
  }

  [[nodiscard]] auto Entries() const noexcept -> std::span<const Entry>
  {
    return entries_;
  }

private:
  std::vector<Entry> entries_ {};
};

} // namespace oxygen::console
