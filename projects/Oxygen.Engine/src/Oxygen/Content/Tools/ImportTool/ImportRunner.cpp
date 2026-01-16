//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Tools/ImportTool/ImportRunner.h>

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::ordered_json;

  auto DurationToMillis(
    const std::optional<std::chrono::microseconds>& duration) -> ordered_json
  {
    if (!duration.has_value()) {
      return nullptr;
    }
    const auto ms = std::chrono::duration<double, std::milli>(*duration);
    return ms.count();
  }

  auto BuildTelemetryJson(const ImportTelemetry& telemetry) -> ordered_json
  {
    return ordered_json {
      { "io_ms", DurationToMillis(telemetry.io_duration) },
      { "decode_ms", DurationToMillis(telemetry.decode_duration) },
      { "load_ms", DurationToMillis(telemetry.load_duration) },
      { "cook_ms", DurationToMillis(telemetry.cook_duration) },
      { "emit_ms", DurationToMillis(telemetry.emit_duration) },
      { "finalize_ms", DurationToMillis(telemetry.finalize_duration) },
      { "total_ms", DurationToMillis(telemetry.total_duration) },
    };
  }

  auto ResolveReportPath(std::string_view report_path,
    const std::filesystem::path& cooked_root, std::ostream& error_stream)
    -> std::optional<std::filesystem::path>
  {
    std::filesystem::path path(report_path);
    if (path.is_absolute()) {
      return path;
    }
    if (cooked_root.empty()) {
      error_stream << "ERROR: --report requires a cooked root when using a "
                      "relative path\n";
      return std::nullopt;
    }
    return (cooked_root / path).lexically_normal();
  }

  auto WriteJsonReport(const ordered_json& payload,
    const std::filesystem::path& report_path, std::ostream& error_stream)
    -> bool
  {
    std::error_code ec;
    const auto parent = report_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        error_stream << "ERROR: failed to create report directory: "
                     << parent.string() << "\n";
        return false;
      }
    }

    std::ofstream output(report_path);
    if (!output) {
      error_stream << "ERROR: failed to open report file: "
                   << report_path.string() << "\n";
      return false;
    }
    output << payload.dump(2) << "\n";
    return true;
  }

} // namespace

auto RunImportJob(const ImportRequest& request, const bool verbose,
  const std::string_view report_path) -> int
{
  const auto start_time = std::chrono::steady_clock::now();
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<ImportReport> report;

  ImportReport report_copy {};
  std::optional<std::string> submit_error;
  bool have_report = false;
  {
    AsyncImportService service;

    const auto on_complete = [&](ImportJobId, const ImportReport& result) {
      {
        std::lock_guard lock(mutex);
        report = result;
      }
      cv.notify_one();
    };

    const auto on_progress = [&](const ImportProgress& progress) {
      if (!verbose) {
        return;
      }
      std::cout << "phase=" << static_cast<uint32_t>(progress.phase)
                << " overall=" << progress.overall_progress << "\n";
    };

    const auto job_id = service.SubmitImport(request, on_complete, on_progress);
    if (job_id == kInvalidJobId) {
      submit_error = "ERROR: failed to submit import job";
    } else {
      std::unique_lock lock(mutex);
      cv.wait(lock, [&] { return report.has_value(); });

      report_copy = *report;
      have_report = true;
    }
  }

  if (submit_error.has_value()) {
    std::cerr << *submit_error << "\n";
    return 2;
  }

  if (!have_report) {
    std::cerr << "ERROR: import failed with no report\n";
    return 2;
  }

  if (!report_path.empty()) {
    const auto cooked_root = report_copy.cooked_root;
    const auto resolved_path
      = ResolveReportPath(report_path, cooked_root, std::cerr);
    if (!resolved_path.has_value()) {
      return 2;
    }

    const auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start_time)
                           .count();
    ordered_json payload = ordered_json::object();
    payload["summary"] = {
      { "jobs", 1 },
      { "succeeded", report_copy.success ? 1 : 0 },
      { "failed", report_copy.success ? 0 : 1 },
      { "total_time_ms", elapsed },
      { "cooked_root", cooked_root.string() },
    };
    payload["jobs"] = ordered_json::array({
      {
        { "index", 1 },
        { "source", request.source_path.string() },
        { "success", report_copy.success },
        { "telemetry", BuildTelemetryJson(report_copy.telemetry) },
      },
    });

    if (!WriteJsonReport(payload, *resolved_path, std::cerr)) {
      return 2;
    }
  }

  if (!report_copy.success) {
    std::cerr << "ERROR: import failed\n";
    for (const auto& diag : report_copy.diagnostics) {
      std::cerr << "- " << diag.code << ": " << diag.message << "\n";
    }
    return 2;
  }

  std::cout << "OK: import complete\n";
  return 0;
}

} // namespace oxygen::content::import::tool
