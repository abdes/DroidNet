//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/Import/ImportProgress.h>
#include <Oxygen/Content/Import/ImportReport.h>

namespace oxygen::content::import::tool {

using nlohmann::ordered_json;

inline constexpr std::string_view kReportVersion = "2";

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
  std::optional<std::chrono::steady_clock::time_point> collected;
};

struct JobProgressTrace {
  std::optional<std::chrono::steady_clock::time_point> started;
  std::optional<std::chrono::steady_clock::time_point> finished;
  std::vector<PhaseTiming> phases = std::vector<PhaseTiming>(PhaseCount());
  std::unordered_map<std::string, ItemTiming> items;
};

[[nodiscard]] auto FormatUtcTimestamp(
  std::chrono::system_clock::time_point time) -> std::string;

[[nodiscard]] auto MakeSessionId(std::chrono::system_clock::time_point time)
  -> std::string;

auto UpdateProgressTrace(JobProgressTrace& trace, const ProgressEvent& progress,
  std::chrono::steady_clock::time_point now) -> void;

[[nodiscard]] auto BuildStatsJson(const ImportTelemetry& telemetry)
  -> ordered_json;

[[nodiscard]] auto BuildEmptyStatsJson() -> ordered_json;

[[nodiscard]] auto ComputeIoMillis(const ImportTelemetry& telemetry) -> double;

[[nodiscard]] auto ComputeCpuMillis(const ImportTelemetry& telemetry) -> double;

[[nodiscard]] auto BuildDiagnosticsJson(
  const std::vector<ImportDiagnostic>& diagnostics) -> ordered_json;

[[nodiscard]] auto BuildOutputsJson(
  const std::vector<ImportOutputRecord>& outputs) -> ordered_json;

[[nodiscard]] auto BuildWorkItemsJson(const JobProgressTrace& trace,
  std::string_view fallback_type, std::string_view fallback_name)
  -> ordered_json;

[[nodiscard]] auto IsCanceledReport(const ImportReport& report) -> bool;

[[nodiscard]] auto JobStatusFromReport(const ImportReport& report)
  -> std::string_view;

[[nodiscard]] auto BuildProgressJson(const JobProgressTrace& trace,
  std::chrono::steady_clock::time_point fallback_start) -> ordered_json;

} // namespace oxygen::content::import::tool
