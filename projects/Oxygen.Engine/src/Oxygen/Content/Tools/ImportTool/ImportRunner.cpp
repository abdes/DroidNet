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
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Tools/ImportTool/ImportRunner.h>

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::ordered_json;

  constexpr auto PhaseCount() -> size_t
  {
    return static_cast<size_t>(ImportPhase::kFailed) + 1U;
  }

  auto PhaseIndex(const ImportPhase phase) -> size_t
  {
    return static_cast<size_t>(phase);
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

  auto ToRelativeMillis(const std::chrono::steady_clock::time_point base,
    const std::optional<std::chrono::steady_clock::time_point>& value)
    -> ordered_json
  {
    if (!value.has_value()) {
      return nullptr;
    }
    const auto ms
      = std::chrono::duration<double, std::milli>(*value - base).count();
    return ms;
  }

  auto BuildProgressJson(const JobProgressTrace& trace,
    const std::chrono::steady_clock::time_point fallback_start) -> ordered_json
  {
    const auto base = trace.started.value_or(fallback_start);
    ordered_json phases = ordered_json::array();
    for (size_t index = 0; index < trace.phases.size(); ++index) {
      const auto& timing = trace.phases[index];
      if (!timing.started.has_value() && !timing.finished.has_value()) {
        continue;
      }

      const auto started_ms = ToRelativeMillis(base, timing.started);
      const auto finished_ms = ToRelativeMillis(base, timing.finished);
      ordered_json duration_ms = nullptr;
      if (timing.started.has_value() && timing.finished.has_value()) {
        duration_ms = std::chrono::duration<double, std::milli>(
          *timing.finished - *timing.started)
                        .count();
      }
      phases.push_back({
        { "phase", nostd::to_string(static_cast<ImportPhase>(index)) },
        { "started_ms", started_ms },
        { "finished_ms", finished_ms },
        { "duration_ms", duration_ms },
        { "items_completed", timing.items_completed },
        { "items_total", timing.items_total },
      });
    }

    ordered_json items = ordered_json::array();
    for (const auto& [key, item] : trace.items) {
      const auto started_ms = ToRelativeMillis(base, item.started);
      const auto finished_ms = ToRelativeMillis(base, item.finished);
      ordered_json duration_ms = nullptr;
      if (item.started.has_value() && item.finished.has_value()) {
        duration_ms = std::chrono::duration<double, std::milli>(
          *item.finished - *item.started)
                        .count();
      }
      items.push_back({
        { "phase", item.phase },
        { "kind", item.kind },
        { "name", item.name },
        { "started_ms", started_ms },
        { "finished_ms", finished_ms },
        { "duration_ms", duration_ms },
      });
    }

    ordered_json job = ordered_json::object();
    job["started_ms"] = ToRelativeMillis(base, trace.started);
    job["finished_ms"] = ToRelativeMillis(base, trace.finished);
    ordered_json job_duration = nullptr;
    if (trace.started.has_value() && trace.finished.has_value()) {
      job_duration = std::chrono::duration<double, std::milli>(
        *trace.finished - *trace.started)
                       .count();
    }
    job["duration_ms"] = job_duration;

    ordered_json progress = ordered_json::object();
    progress["job"] = std::move(job);
    progress["phases"] = std::move(phases);
    progress["items"] = std::move(items);
    return progress;
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

auto RunImportJob(const ImportRequest& request, const bool quiet,
  const std::string_view report_path) -> int
{
  const auto start_time = std::chrono::steady_clock::now();
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<ImportReport> report;
  JobProgressTrace progress_trace {};

  ImportReport report_copy {};
  std::optional<std::string> submit_error;
  bool have_report = false;
  {
    AsyncImportService service;

    const auto on_complete = [&](ImportJobId, const ImportReport& result) {
      {
        std::scoped_lock lock(mutex);
        report = result;
      }
      cv.notify_one();
    };

    const auto on_progress = [&](const ProgressEvent& progress) {
      const auto now = std::chrono::steady_clock::now();
      {
        std::scoped_lock lock(mutex);
        if (!progress_trace.started.has_value()) {
          progress_trace.started = now;
        }

        if (progress.header.kind == ProgressEventKind::kJobStarted) {
          progress_trace.started = now;
        } else if (progress.header.kind == ProgressEventKind::kJobFinished) {
          progress_trace.finished = now;
        }

        const auto phase_index = PhaseIndex(progress.header.phase);
        if (progress.header.kind == ProgressEventKind::kPhaseUpdate
          && phase_index < progress_trace.phases.size()) {
          auto& timing = progress_trace.phases[phase_index];
          if (!timing.started.has_value()) {
            timing.started = now;
          }
          if (phase_index > 0U) {
            auto& previous = progress_trace.phases[phase_index - 1U];
            if (!previous.finished.has_value()) {
              previous.finished = now;
            }
          }
          if (progress.header.phase == ImportPhase::kComplete
            || progress.header.phase == ImportPhase::kFailed
            || progress.header.phase == ImportPhase::kCancelled) {
            for (auto& entry : progress_trace.phases) {
              if (!entry.finished.has_value() && entry.started.has_value()) {
                entry.finished = now;
              }
            }
          }
        }

        if (const auto* item = GetItemProgress(progress)) {
          if (!item->item_name.empty()) {
            std::string key = item->item_kind;
            if (!key.empty()) {
              key.append(":");
            }
            key.append(item->item_name);
            auto& trace_item = progress_trace.items[key];
            trace_item.phase = nostd::to_string(progress.header.phase);
            trace_item.kind = item->item_kind;
            trace_item.name = item->item_name;
            if (progress.header.kind == ProgressEventKind::kItemStarted) {
              trace_item.started = now;
            } else if (progress.header.kind
              == ProgressEventKind::kItemFinished) {
              trace_item.finished = now;
            }
          }
        }
      }

      if (!quiet) {
        std::cout << "event=" << to_string(progress.header.kind)
                  << " phase=" << nostd::to_string(progress.header.phase)
                  << " overall=" << progress.header.overall_progress;
        if (const auto* item = GetItemProgress(progress)) {
          if (!item->item_name.empty()) {
            std::cout << " item=" << item->item_name;
          }
        }
        std::cout << "\n";
      }
    };

    LOG_F(INFO, "ImportTool submit job: source='{}' with_content_hashing={}",
      request.source_path.string(), request.options.with_content_hashing);
    const auto job_id = service.SubmitImport(request, on_complete, on_progress);
    if (!job_id) {
      submit_error = "ERROR: failed to submit import job";
    } else {
      std::unique_lock lock(mutex);
      cv.wait(lock, [&] { return report.has_value(); });

      report_copy = *report;
      have_report = true;
    }

    service.Stop();
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
        { "progress", BuildProgressJson(progress_trace, start_time) },
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

  if (!quiet) {
    std::cout << "OK: import complete\n";
  }
  return 0;
}

} // namespace oxygen::content::import::tool
