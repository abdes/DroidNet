//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <ctime>
#include <iomanip>
#include <ranges>
#include <sstream>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Tools/ImportTool/ReportJson.h>

namespace oxygen::content::import::tool {

namespace {

  auto PhaseIndex(const ImportPhase phase) -> size_t
  {
    return static_cast<size_t>(phase);
  }

  auto DurationToMillis(
    const std::optional<std::chrono::microseconds>& duration,
    const std::string_view label) -> double
  {
    CHECK_F(duration.has_value(),
      "Missing telemetry duration for '{}' in report output", label);
    const auto ms = std::chrono::duration<double, std::milli>(*duration);
    return ms.count();
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

  auto SeverityToString(const ImportSeverity severity) -> std::string_view
  {
    switch (severity) {
    case ImportSeverity::kInfo:
      return "info";
    case ImportSeverity::kWarning:
      return "warning";
    case ImportSeverity::kError:
      return "error";
    }
    return "info";
  }

  auto MakeWorkItem(std::string_view type, std::string_view name)
    -> ordered_json
  {
    return ordered_json {
      { "type", std::string(type) },
      { "name", std::string(name) },
    };
  }

} // namespace

auto FormatUtcTimestamp(const std::chrono::system_clock::time_point time)
  -> std::string
{
  const auto as_time_t = std::chrono::system_clock::to_time_t(time);
  std::tm utc {};
  gmtime_s(&utc, &as_time_t);
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

auto MakeSessionId(const std::chrono::system_clock::time_point time)
  -> std::string
{
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    time.time_since_epoch())
                    .count();
  return fmt::format("session-{}", ms);
}

auto UpdateProgressTrace(JobProgressTrace& trace, const ProgressEvent& progress,
  const std::chrono::steady_clock::time_point now) -> void
{
  if (!trace.started.has_value()) {
    trace.started = now;
  }

  if (progress.header.kind == ProgressEventKind::kJobStarted) {
    trace.started = now;
  } else if (progress.header.kind == ProgressEventKind::kJobFinished) {
    trace.finished = now;
  }

  const auto phase_index = PhaseIndex(progress.header.phase);
  if (progress.header.kind == ProgressEventKind::kPhaseUpdate
    && phase_index < trace.phases.size()) {
    auto& timing = trace.phases[phase_index];
    if (!timing.started.has_value()) {
      timing.started = now;
    }
    if (phase_index > 0U) {
      auto& previous = trace.phases[phase_index - 1U];
      if (!previous.finished.has_value()) {
        previous.finished = now;
      }
    }
    if (progress.header.phase == ImportPhase::kComplete
      || progress.header.phase == ImportPhase::kFailed
      || progress.header.phase == ImportPhase::kCancelled) {
      for (auto& entry : trace.phases) {
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
      auto& trace_item = trace.items[key];
      trace_item.phase = nostd::to_string(progress.header.phase);
      trace_item.kind = item->item_kind;
      trace_item.name = item->item_name;
      if (progress.header.kind == ProgressEventKind::kItemStarted) {
        trace_item.started = now;
      } else if (progress.header.kind == ProgressEventKind::kItemFinished) {
        trace_item.finished = now;
      } else if (progress.header.kind == ProgressEventKind::kItemCollected) {
        trace_item.collected = now;
      }
    }
  }
}

auto BuildStatsJson(const ImportTelemetry& telemetry) -> ordered_json
{
  return ordered_json {
    { "time_ms_total", DurationToMillis(telemetry.total_duration, "total") },
    { "time_ms_io", DurationToMillis(telemetry.io_duration, "io") },
    { "time_ms_source_load",
      DurationToMillis(telemetry.source_load_duration, "source_load") },
    { "time_ms_decode", DurationToMillis(telemetry.decode_duration, "decode") },
    { "time_ms_load", DurationToMillis(telemetry.load_duration, "load") },
    { "time_ms_cook", DurationToMillis(telemetry.cook_duration, "cook") },
    { "time_ms_emit", DurationToMillis(telemetry.emit_duration, "emit") },
    { "time_ms_finalize",
      DurationToMillis(telemetry.finalize_duration, "finalize") },
  };
}

auto BuildEmptyStatsJson() -> ordered_json
{
  return ordered_json {
    { "time_ms_total", 0.0 },
    { "time_ms_io", 0.0 },
    { "time_ms_source_load", 0.0 },
    { "time_ms_decode", 0.0 },
    { "time_ms_load", 0.0 },
    { "time_ms_cook", 0.0 },
    { "time_ms_emit", 0.0 },
    { "time_ms_finalize", 0.0 },
  };
}

auto ComputeIoMillis(const ImportTelemetry& telemetry) -> double
{
  return DurationToMillis(telemetry.io_duration, "io");
}

auto ComputeCpuMillis(const ImportTelemetry& telemetry) -> double
{
  return DurationToMillis(telemetry.decode_duration, "decode")
    + DurationToMillis(telemetry.load_duration, "load")
    + DurationToMillis(telemetry.cook_duration, "cook")
    + DurationToMillis(telemetry.emit_duration, "emit")
    + DurationToMillis(telemetry.finalize_duration, "finalize");
}

auto BuildDiagnosticsJson(const std::vector<ImportDiagnostic>& diagnostics)
  -> ordered_json
{
  ordered_json entries = ordered_json::array();
  std::ranges::for_each(diagnostics, [&](const ImportDiagnostic& diag) {
    ordered_json entry = ordered_json::object();
    entry["severity"] = std::string(SeverityToString(diag.severity));
    entry["code"] = diag.code;
    entry["message"] = diag.message;
    if (!diag.source_path.empty()) {
      entry["source_path"] = diag.source_path;
    }
    if (!diag.object_path.empty()) {
      entry["object_path"] = diag.object_path;
    }
    entries.push_back(std::move(entry));
  });
  return entries;
}

auto BuildOutputsJson(const std::vector<ImportOutputRecord>& outputs)
  -> ordered_json
{
  ordered_json entries = ordered_json::array();
  for (const auto& output : outputs) {
    CHECK_F(!output.path.empty(), "Output path must be non-empty");
    entries.push_back({
      { "path", output.path },
      { "size_bytes", output.size_bytes },
    });
  }
  return entries;
}

auto BuildWorkItemsJson(const JobProgressTrace& trace,
  const std::string_view fallback_type, const std::string_view fallback_name)
  -> ordered_json
{
  ordered_json items = ordered_json::array();
  std::optional<std::chrono::steady_clock::time_point> base = trace.started;
  if (!base.has_value()) {
    for (const auto& [key, item] : trace.items) {
      (void)key;
      if (item.started.has_value()) {
        if (!base.has_value() || *item.started < *base) {
          base = item.started;
        }
      }
    }
  }
  for (const auto& [key, item] : trace.items) {
    (void)key;
    if (item.name.empty()) {
      continue;
    }
    const auto type = item.kind.empty() ? fallback_type : item.kind;
    ordered_json started_ms = nullptr;
    ordered_json finished_ms = nullptr;
    ordered_json collected_ms = nullptr;
    if (base.has_value()) {
      started_ms = ToRelativeMillis(*base, item.started);
      finished_ms = ToRelativeMillis(*base, item.finished);
      collected_ms = ToRelativeMillis(*base, item.collected);
    }
    items.push_back({
      { "type", std::string(type) },
      { "name", item.name },
      { "started_ms", started_ms },
      { "finished_ms", finished_ms },
      { "collected_ms", collected_ms },
    });
  }

  if (items.empty()) {
    items.push_back(MakeWorkItem(fallback_type, fallback_name));
  }

  CHECK_F(!items.empty(), "Work items must be non-empty");
  return items;
}

auto IsCanceledReport(const ImportReport& report) -> bool
{
  for (const auto& diag : report.diagnostics) {
    if (diag.code == "import.canceled") {
      return true;
    }
  }
  return false;
}

auto JobStatusFromReport(const ImportReport& report) -> std::string_view
{
  if (report.success) {
    return "succeeded";
  }
  if (IsCanceledReport(report)) {
    return "skipped";
  }
  return "failed";
}

auto BuildProgressJson(const JobProgressTrace& trace,
  std::chrono::steady_clock::time_point fallback_start) -> ordered_json
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
    const auto collected_ms = ToRelativeMillis(base, item.collected);
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
      { "collected_ms", collected_ms },
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

} // namespace oxygen::content::import::tool
