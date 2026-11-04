//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Tools/Common/ReportFile.h>

namespace oxygen::content::import::tool {

using nlohmann::ordered_json;

//! The `schema_version` an import report carries.
inline constexpr int kReportSchemaVersion = 6;

class IMessageWriter;

//! The refusal codes of a command, before a request or a job can state its own.
namespace import_tool_codes {
  //! A command line option's value or combination is refused.
  inline constexpr std::string_view kOptionInvalid = "import.option_invalid";
  //! The import service did not accept the job.
  inline constexpr std::string_view kSubmitFailed = "import.submit_failed";
  //! A system call failed before the job could be submitted.
  inline constexpr std::string_view kCommandFailed = "import.command_failed";
  //! A predecessor of the job failed or was refused, so the job did not run.
  inline constexpr std::string_view kSkippedPredecessorFailed
    = "import.skipped_predecessor_failed";
} // namespace import_tool_codes

//! Whether a report may be written to `report_path`; prints the typed refusal
//! when it may not. A command without a report has nothing to refuse.
[[nodiscard]] auto IsReportDestinationSafe(std::string_view report_path,
  const toolkit::ReportGuard& guard, observer_ptr<IMessageWriter> writer)
  -> bool;

//! Publishes a report to the file `report_path` names; prints the typed failure
//! and returns false when it is not written.
[[nodiscard]] auto PublishImportReport(std::string_view report_path,
  std::string_view text, observer_ptr<IMessageWriter> writer) -> bool;

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
  std::span<const ImportDiagnostic> diagnostics) -> ordered_json;

//! The importer that cooks `request` and its revision, as `name` and
//! `revision`, or nullopt when no importer handles the request.
[[nodiscard]] auto BuildImporterJson(const ImportRequest& request)
  -> std::optional<ordered_json>;

[[nodiscard]] auto BuildOutputsJson(
  const std::vector<ImportOutputRecord>& outputs) -> ordered_json;

[[nodiscard]] auto BuildProductsJson(
  const std::vector<ImportProductRecord>& products) -> ordered_json;

[[nodiscard]] auto BuildWorkItemsJson(const JobProgressTrace& trace,
  std::string_view fallback_type, std::string_view fallback_name)
  -> ordered_json;

[[nodiscard]] auto IsCanceledReport(const ImportReport& report) -> bool;

//! The result of a job that did not run because `predecessor` failed or was
//! refused.
[[nodiscard]] auto MakeSkippedReport(
  const ImportRequest& request, std::string_view predecessor) -> ImportReport;

//! The result of a job that the run ended before it ran.
[[nodiscard]] auto MakeCancelledReport(const ImportRequest& request)
  -> ImportReport;

//! The job's status in a report: `succeeded`, `failed`, `skipped` or
//! `cancelled`. A job the run never reached has no result and is
//! `not_submitted`.
[[nodiscard]] auto JobStatusFromReport(const ImportReport& report)
  -> std::string_view;

} // namespace oxygen::content::import::tool
