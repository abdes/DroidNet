//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/Import/ImportProgress.h>
#include <Oxygen/Content/Import/ImportReport.h>

namespace oxygen::content::import::tool {

using nlohmann::ordered_json;

constexpr auto PhaseCount() -> size_t
{
  return static_cast<size_t>(ImportPhase::kFailed) + 1U;
}

struct PhaseTiming {
  std::optional<std::chrono::steady_clock::time_point> started;
  std::optional<std::chrono::steady_clock::time_point> finished;
  uint32_t items_completed = 0U;
  uint32_t items_total = 0U;
};

struct ItemTiming {
  std::string phase;
  std::string kind;
  std::string name;
  std::optional<std::chrono::steady_clock::time_point> started;
  std::optional<std::chrono::steady_clock::time_point> finished;
};

struct JobProgressTrace {
  std::optional<std::chrono::steady_clock::time_point> started;
  std::optional<std::chrono::steady_clock::time_point> finished;
  std::vector<PhaseTiming> phases = std::vector<PhaseTiming>(PhaseCount());
  std::unordered_map<std::string, ItemTiming> items;
};

[[nodiscard]] auto BuildTelemetryJson(const ImportTelemetry& telemetry)
  -> ordered_json;

[[nodiscard]] auto BuildProgressJson(const JobProgressTrace& trace,
  std::chrono::steady_clock::time_point fallback_start) -> ordered_json;

} // namespace oxygen::content::import::tool
