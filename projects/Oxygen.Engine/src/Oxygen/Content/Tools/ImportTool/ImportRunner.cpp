//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_map>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Content/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Content/Tools/ImportTool/ReportJson.h>
#include <Oxygen/Content/Tools/ImportTool/UI/JobViewModel.h>
#include <Oxygen/Content/Tools/ImportTool/UI/Screens/ImportScreen.h>

#ifndef OXYGEN_IMPORT_TOOL_VERSION
#  error OXYGEN_IMPORT_TOOL_VERSION must be defined for ImportTool reports.
#endif

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::ordered_json;

  auto ResolveReportPath(std::string_view report_path,
    const std::filesystem::path& cooked_root,
    const oxygen::observer_ptr<IMessageWriter>& writer)
    -> std::optional<std::filesystem::path>
  {
    std::filesystem::path path(report_path);
    if (path.is_absolute()) {
      return path;
    }
    if (cooked_root.empty()) {
      if (writer) {
        writer->Error(
          "ERROR: --report requires a cooked root when using a relative path");
      }
      return std::nullopt;
    }
    return (cooked_root / path).lexically_normal();
  }

  auto WriteJsonReport(const ordered_json& payload,
    const std::filesystem::path& report_path,
    const oxygen::observer_ptr<IMessageWriter>& writer) -> bool
  {
    std::error_code ec;
    const auto parent = report_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        if (writer) {
          writer->Error(fmt::format(
            "ERROR: failed to create report directory: {}", parent.string()));
        }
        return false;
      }
    }

    std::ofstream output(report_path);
    if (!output) {
      if (writer) {
        writer->Error(fmt::format(
          "ERROR: failed to open report file: {}", report_path.string()));
      }
      return false;
    }
    output << payload.dump(2) << "\n";
    return true;
  }

} // namespace

auto RunImportJob(const ImportRequest& request,
  oxygen::observer_ptr<IMessageWriter> writer,
  const std::string_view report_path, const std::string_view command_line,
  const bool enable_tui, oxygen::observer_ptr<AsyncImportService> service)
  -> std::expected<void, std::error_code>
{
  const auto start_time = std::chrono::steady_clock::now();
  const auto session_started = std::chrono::system_clock::now();
  DCHECK_NOTNULL_F(writer, "Message writer must be provided by main");
  DCHECK_NOTNULL_F(service, "Import service must be provided by main");
  CHECK_F(!command_line.empty(), "Command line is required for report output");

  std::mutex mutex;
  std::condition_variable cv;
  std::optional<ImportReport> report;
  JobProgressTrace progress_trace {};

  ImportReport report_copy {};
  std::optional<std::string> submit_error;
  bool have_report = false;
  std::vector<std::string> recent_logs;
  {
    const auto on_complete = [&](ImportJobId, const ImportReport& result) {
      {
        std::scoped_lock lock(mutex);
        report = result;
        recent_logs.push_back(
          fmt::format("Job Completed: {}", result.success ? "OK" : "FAIL"));
        if (recent_logs.size() > 50) {
          recent_logs.erase(recent_logs.begin(), recent_logs.end() - 50);
        }
      }
      cv.notify_one();
    };

    const auto on_progress = [&](const ProgressEvent& progress) {
      const auto now = std::chrono::steady_clock::now();
      {
        std::scoped_lock lock(mutex);
        UpdateProgressTrace(progress_trace, progress, now);

        if (progress.header.kind == ProgressEventKind::kJobStarted) {
          recent_logs.push_back("Job Started");
        } else if (progress.header.kind == ProgressEventKind::kJobFinished) {
          recent_logs.push_back("Job Finished");
        }

        if (const auto* item = GetItemProgress(progress)) {
          if (!item->item_name.empty()) {
            if (progress.header.kind == ProgressEventKind::kItemStarted) {
              recent_logs.push_back(fmt::format("Started {}", item->item_name));
            } else if (progress.header.kind
              == ProgressEventKind::kItemFinished) {
              recent_logs.push_back(
                fmt::format("Finished {}", item->item_name));
            }
          }
        }

        if (recent_logs.size() > 50) {
          recent_logs.erase(recent_logs.begin(), recent_logs.end() - 50);
        }
      }

      {
        std::string msg = fmt::format("event={} phase={} overall={}",
          to_string(progress.header.kind),
          nostd::to_string(progress.header.phase),
          progress.header.overall_progress);
        if (const auto* item = GetItemProgress(progress)) {
          if (!item->item_name.empty()) {
            msg.append(fmt::format(" item={}", item->item_name));
          }
        }
        writer->Progress(msg);
      }
    };

    LOG_F(INFO, "ImportTool submit job: source='{}' with_content_hashing={}",
      request.source_path.string(), request.options.with_content_hashing);
    const auto job_id
      = service->SubmitImport(request, on_complete, on_progress);
    if (!job_id) {
      submit_error = "ERROR: failed to submit import job";
    } else {
      // If TUI is enabled and not quiet, run the interactive screen while job
      // runs
      if (enable_tui) {
        ImportScreen screen;
        screen.SetDataProvider([&]() {
          std::scoped_lock lock(mutex);
          JobViewModel vm;
          // derive progress from phases if available
          if (!progress_trace.phases.empty()) {
            const auto& p = progress_trace.phases.back();
            vm.progress = p.items_total == 0
              ? 0.0f
              : static_cast<float>(p.items_completed)
                / static_cast<float>(p.items_total);
          } else {
            vm.progress = 0.0f;
          }
          vm.status = report.has_value()
            ? (report->success ? "Completed" : "Failed")
            : "Running";
          vm.recent_logs = recent_logs;
          if (progress_trace.started.has_value()) {
            vm.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - *progress_trace.started);
          }
          vm.completed = report.has_value();
          vm.success = report.has_value() ? report->success : false;
          if (vm.completed) {
            vm.progress = 1.0f;
          }
          return vm;
        });

        // The provided writer for TUI runs should already be muted.
        screen.Run();
      }

      std::unique_lock lock(mutex);
      cv.wait(lock, [&] { return report.has_value(); });

      report_copy = *report;
      have_report = true;
    }

    service->Stop();
  }

  if (submit_error.has_value()) {
    writer->Error(*submit_error);
    return std::unexpected(
      std::make_error_code(std::errc::state_not_recoverable));
  }

  if (!have_report) {
    writer->Error("ERROR: import failed with no report");
    CHECK_F(false, "Import completed without a report");
    return std::unexpected(
      std::make_error_code(std::errc::state_not_recoverable));
  }

  std::optional<std::filesystem::path> written_report;
  if (!report_path.empty()) {
    const auto cooked_root = report_copy.cooked_root;
    const auto resolved_path
      = ResolveReportPath(report_path, cooked_root, writer);
    if (!resolved_path.has_value()) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    const auto session_ended = std::chrono::system_clock::now();
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start_time)
                              .count();
    CHECK_F(!cooked_root.empty(), "Cooked root is required in report output");

    ordered_json payload = ordered_json::object();
    payload["report_version"] = std::string(kReportVersion);
    payload["session"] = {
      { "id", MakeSessionId(session_started) },
      { "started_utc", FormatUtcTimestamp(session_started) },
      { "ended_utc", FormatUtcTimestamp(session_ended) },
      { "tool_version", std::string(OXYGEN_IMPORT_TOOL_VERSION) },
      { "command_line", std::string(command_line) },
      { "cooked_root", cooked_root.string() },
    };

    const auto stats_json = BuildStatsJson(report_copy.telemetry);
    const auto time_ms_io = ComputeIoMillis(report_copy.telemetry);
    const auto time_ms_cpu = ComputeCpuMillis(report_copy.telemetry);

    payload["summary"] = {
      { "jobs_total", 1 },
      { "jobs_succeeded", report_copy.success ? 1 : 0 },
      { "jobs_failed", report_copy.success ? 0 : 1 },
      { "jobs_skipped", 0 },
      { "time_ms_total", elapsed_ms },
      { "time_ms_io", time_ms_io },
      { "time_ms_cpu", time_ms_cpu },
    };

    const auto job_type
      = std::string(request.GetFormat() != ImportFormat::kUnknown
          ? to_string(request.GetFormat())
          : "unknown");
    ordered_json job = ordered_json::object();
    job["index"] = 1;
    job["type"] = job_type;
    job["work_items"] = BuildWorkItemsJson(
      progress_trace, job_type, request.source_path.string());
    job["status"] = std::string(JobStatusFromReport(report_copy));
    job["outputs"] = BuildOutputsJson(report_copy.outputs);
    job["stats"] = stats_json;
    job["diagnostics"] = BuildDiagnosticsJson(report_copy.diagnostics);
    payload["jobs"] = ordered_json::array({ std::move(job) });

    if (!WriteJsonReport(payload, *resolved_path, writer)) {
      return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    written_report = *resolved_path;
  }

  if (!report_copy.success) {
    writer->Error("ERROR: import failed");
    for (const auto& diag : report_copy.diagnostics) {
      const auto message = fmt::format("- {}: {}", diag.code, diag.message);
      switch (diag.severity) {
      case ImportSeverity::kInfo:
        writer->Info(message);
        break;
      case ImportSeverity::kWarning:
        writer->Warning(message);
        break;
      case ImportSeverity::kError:
        writer->Error(message);
        break;
      }
    }
    if (written_report.has_value()) {
      writer->Info(fmt::format("Report written: {}", written_report->string()));
    }
    return std::unexpected(
      std::make_error_code(std::errc::state_not_recoverable));
  }

  writer->Report("OK: import complete");
  if (written_report.has_value()) {
    writer->Info(fmt::format("Report written: {}", written_report->string()));
  }
  return {};
}

} // namespace oxygen::content::import::tool
