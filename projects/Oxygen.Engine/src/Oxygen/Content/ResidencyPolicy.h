//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace oxygen::content {

enum class ResidencyTrimMode : uint8_t {
  kManual = 0,
  kAutoOnOverBudget = 1,
};

[[nodiscard]] constexpr auto to_string(const ResidencyTrimMode mode) noexcept
  -> const char*
{
  switch (mode) {
  case ResidencyTrimMode::kManual:
    return "manual";
  case ResidencyTrimMode::kAutoOnOverBudget:
    return "auto_on_over_budget";
  }
  return "__Unknown__";
}

enum class LoadPriorityClass : uint8_t {
  kBackground = 0,
  kDefault = 1,
  kCritical = 2,
};

[[nodiscard]] constexpr auto to_string(
  const LoadPriorityClass priority) noexcept -> const char*
{
  switch (priority) {
  case LoadPriorityClass::kBackground:
    return "background";
  case LoadPriorityClass::kDefault:
    return "default";
  case LoadPriorityClass::kCritical:
    return "critical";
  }
  return "__Unknown__";
}

enum class LoadPriority : uint8_t {
  kBackground = 0,
  kDefault = 1,
  kCritical = 2,
};

[[nodiscard]] constexpr auto to_string(const LoadPriority priority) noexcept
  -> const char*
{
  switch (priority) {
  case LoadPriority::kBackground:
    return "background";
  case LoadPriority::kDefault:
    return "default";
  case LoadPriority::kCritical:
    return "critical";
  }
  return "__Unknown__";
}

enum class LoadIntent : uint8_t {
  kRuntime = 0,
  kPrewarm = 1,
  kStreaming = 2,
};

[[nodiscard]] constexpr auto to_string(const LoadIntent intent) noexcept
  -> const char*
{
  switch (intent) {
  case LoadIntent::kRuntime:
    return "runtime";
  case LoadIntent::kPrewarm:
    return "prewarm";
  case LoadIntent::kStreaming:
    return "streaming";
  }
  return "__Unknown__";
}

struct LoadRequest final {
  LoadPriority priority { LoadPriority::kDefault };
  LoadIntent intent { LoadIntent::kRuntime };
};

struct ResidencyPolicy final {
  uint64_t cache_budget_bytes { (std::numeric_limits<uint64_t>::max)() };
  ResidencyTrimMode trim_mode { ResidencyTrimMode::kManual };
  LoadPriorityClass default_priority_class { LoadPriorityClass::kDefault };
};

struct ResidencyPolicyState final {
  ResidencyPolicy policy {};
  std::size_t cache_entries { 0 };
  uint64_t consumed_bytes { 0 };
  std::size_t checked_out_items { 0 };
  bool over_budget { false };
};

} // namespace oxygen::content
