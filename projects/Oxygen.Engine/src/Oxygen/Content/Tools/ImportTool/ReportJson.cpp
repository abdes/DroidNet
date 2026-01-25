//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Tools/ImportTool/ReportJson.h>

namespace oxygen::content::import::tool {

namespace {

  auto DurationToMillis(
    const std::optional<std::chrono::microseconds>& duration) -> ordered_json
  {
    if (!duration.has_value()) {
      return nullptr;
    }
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

} // namespace

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

} // namespace oxygen::content::import::tool
