//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <clocale>
#include <condition_variable>
#include <conio.h>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include <curses.h>

#include <nlohmann/json.hpp>

#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Content/Tools/ImportTool/ImportManifest.h>
#include <Oxygen/Content/Tools/ImportTool/TextureImportRequestBuilder.h>
#include <Oxygen/OxCo/Detail/ScopeGuard.h>

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::ordered_json;
  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;
  using oxygen::co::detail::ScopeGuard;

  auto ShouldOverride(const bool override_value, const bool current_value)
    -> bool
  {
    return override_value && !current_value;
  }

  auto DisplayJobNumber(const size_t job_index) -> size_t
  {
    return job_index + 1;
  }

  auto PrintDiagnostics(const ImportReport& report, const size_t job_index,
    std::string_view source_path) -> void
  {
    if (report.success) {
      return;
    }

    std::cerr << "ERROR: import failed (job=" << DisplayJobNumber(job_index)
              << ", source=" << source_path << ")\n";
    for (const auto& diag : report.diagnostics) {
      std::cerr << "- " << diag.code << ": " << diag.message << "\n";
    }
  }

  struct PreparedJob {
    ImportRequest request;
    bool verbose = false;
    std::string source_path;
  };

  struct UiEvent {
    std::string text;
    bool is_error = false;
  };

  struct PendingJobSnapshot {
    size_t index = 0;
    std::optional<ImportJobId> job_id;
    std::string source_path;
    std::optional<ImportProgress> progress;
    std::optional<std::chrono::steady_clock::time_point> progress_time;
    std::optional<std::chrono::steady_clock::time_point> submit_time;
  };

  struct UiSnapshot {
    size_t completed = 0;
    size_t total = 0;
    size_t in_flight = 0;
    size_t remaining = 0;
    size_t failures = 0;
    std::chrono::seconds elapsed { 0 };
    std::vector<PendingJobSnapshot> pending;
    std::vector<std::string> failed_lines;
    std::deque<UiEvent> recent;
  };

  auto BuildPendingSnapshot(
    const std::vector<std::optional<ImportReport>>& reports,
    const std::vector<PreparedJob>& jobs,
    const std::vector<std::optional<ImportJobId>>& job_ids,
    const std::vector<std::optional<ImportProgress>>& progress_states,
    const std::vector<std::optional<std::chrono::steady_clock::time_point>>
      progress_times,
    const std::vector<std::optional<std::chrono::steady_clock::time_point>>
      submit_times,
    const size_t max_items) -> std::vector<PendingJobSnapshot>
  {
    std::vector<PendingJobSnapshot> pending;
    pending.reserve(std::min(max_items, jobs.size()));
    for (size_t index = 0; index < jobs.size(); ++index) {
      if (reports[index].has_value()) {
        continue;
      }
      pending.push_back({
        .index = DisplayJobNumber(index),
        .job_id = job_ids[index],
        .source_path = jobs[index].source_path,
        .progress = progress_states[index],
        .progress_time = progress_times[index],
        .submit_time = submit_times[index],
      });
      if (pending.size() >= max_items) {
        break;
      }
    }
    return pending;
  }

  auto BuildFailedLines(const std::vector<std::optional<ImportReport>>& reports,
    const std::vector<PreparedJob>& jobs, const size_t max_lines)
    -> std::vector<std::string>
  {
    std::vector<std::string> lines;
    lines.reserve(max_lines);
    for (size_t index = 0; index < reports.size(); ++index) {
      if (!reports[index].has_value() || reports[index]->success) {
        continue;
      }
      std::ostringstream header;
      header << "job=" << DisplayJobNumber(index)
             << " source=" << jobs[index].source_path << " failed";
      lines.push_back(header.str());
      if (lines.size() >= max_lines) {
        break;
      }
      for (const auto& diag : reports[index]->diagnostics) {
        std::ostringstream line;
        line << "  " << diag.code << ": " << diag.message;
        lines.push_back(line.str());
        if (lines.size() >= max_lines) {
          break;
        }
      }
      if (lines.size() >= max_lines) {
        break;
      }
    }
    return lines;
  }

  auto TrimRight(const std::string& value) -> std::string
  {
    auto trimmed = value;
    while (!trimmed.empty() && std::isspace(trimmed.back()) != 0) {
      trimmed.pop_back();
    }
    return trimmed;
  }

  auto AddEvent(
    std::deque<UiEvent>& events, UiEvent event, const size_t max_events) -> void
  {
    events.push_back(std::move(event));
    while (events.size() > max_events) {
      events.pop_front();
    }
  }

  auto EmitEvent(std::mutex& output_mutex, std::deque<UiEvent>& events,
    UiEvent event, const size_t max_events, const bool tui_enabled) -> void
  {
    if (tui_enabled) {
      std::lock_guard lock(output_mutex);
      AddEvent(events, std::move(event), max_events);
      return;
    }

    std::lock_guard lock(output_mutex);
    auto& out = event.is_error ? std::cerr : std::cout;
    out << event.text << "\n";
  }

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

  auto ResolveCookedRootForReport(const std::vector<PreparedJob>& jobs,
    const std::vector<std::optional<ImportReport>>& reports)
    -> std::optional<std::filesystem::path>
  {
    for (const auto& report : reports) {
      if (!report.has_value()) {
        continue;
      }
      if (!report->cooked_root.empty()) {
        return report->cooked_root;
      }
    }
    for (const auto& job : jobs) {
      if (job.request.cooked_root.has_value()) {
        return *job.request.cooked_root;
      }
    }
    return std::nullopt;
  }

  auto ResolveReportPath(std::string_view report_path,
    const std::optional<std::filesystem::path>& cooked_root,
    std::ostream& error_stream) -> std::optional<std::filesystem::path>
  {
    std::filesystem::path path(report_path);
    if (path.is_absolute()) {
      return path;
    }
    if (!cooked_root.has_value()) {
      error_stream << "ERROR: --report requires a cooked root when using a "
                      "relative path\n";
      return std::nullopt;
    }
    return (*cooked_root / path).lexically_normal();
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

  auto WaitForExitKey() -> void
  {
#if defined(_WIN32)
    (void)_getch();
#else
    std::cin.get();
#endif
  }

  auto RenderTui(const UiSnapshot& snapshot) -> void
  {
    constexpr wchar_t kBarFilled = L'█';
    constexpr wchar_t kBarEmpty = L'░';

    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);

    erase();

    mvprintw(0, 0, "Async Import Batch");
    mvprintw(1, 0,
      "Completed: %zu/%zu  In-flight: %zu  Pending: %zu  Failures: %zu  "
      "Elapsed: %lds",
      snapshot.completed, snapshot.total, snapshot.in_flight,
      snapshot.remaining, snapshot.failures,
      static_cast<long>(snapshot.elapsed.count()));

    const int bar_row = 2;
    const int bar_width = std::max(10, cols - 2);
    const double ratio = snapshot.total > 0
      ? static_cast<double>(snapshot.completed)
        / static_cast<double>(snapshot.total)
      : 0.0;
    const int filled = static_cast<int>(ratio * bar_width);
    std::wstring bar;
    bar.reserve(static_cast<size_t>(bar_width) + 2);
    bar.push_back(L'[');
    for (int i = 0; i < bar_width; ++i) {
      bar.push_back(i < filled ? kBarFilled : kBarEmpty);
    }
    bar.push_back(L']');
    mvaddwstr(bar_row, 0, bar.c_str());

    int row = 4;
    mvprintw(row++, 0, "Recent events:");
    for (const auto& event : snapshot.recent) {
      if (row >= rows - 2) {
        break;
      }
      const auto line = TrimRight(event.text);
      if (event.is_error) {
        attron(A_BOLD);
      }
      mvprintw(row++, 2, "%.*s", cols - 4, line.c_str());
      if (event.is_error) {
        attroff(A_BOLD);
      }
    }

    if (snapshot.pending.empty()) {
      if (row < rows - 1) {
        mvprintw(row++, 0, "Failed jobs:");
      }
      if (snapshot.failed_lines.empty()) {
        if (row < rows - 1) {
          mvprintw(row++, 2, "None");
        }
      } else {
        for (const auto& line : snapshot.failed_lines) {
          if (row >= rows - 1) {
            break;
          }
          mvprintw(row++, 2, "%.*s", cols - 4, line.c_str());
        }
      }
    } else {
      if (row < rows - 1) {
        mvprintw(
          row++, 0, "Pending jobs (first %zu):", snapshot.pending.size());
      }
      for (const auto& pending : snapshot.pending) {
        if (row >= rows - 1) {
          break;
        }
        std::ostringstream line;
        line << "job=" << pending.index << " id="
             << (pending.job_id.has_value() ? std::to_string(*pending.job_id)
                                            : std::string("n/a"))
             << " source=" << pending.source_path;
        if (pending.progress.has_value()) {
          const auto phase = static_cast<uint32_t>(pending.progress->phase);
          line << " phase=" << phase;
        }
        if (pending.progress_time.has_value()) {
          const auto age = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - *pending.progress_time);
          line << " last_progress=" << age.count() << "s";
        } else if (pending.submit_time.has_value()) {
          const auto age = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - *pending.submit_time);
          line << " submitted=" << age.count() << "s";
        }
        const auto text = line.str();
        mvprintw(row++, 2, "%.*s", cols - 4, text.c_str());

        if (!pending.progress.has_value() || row >= rows - 1) {
          continue;
        }
        const double progress_ratio = std::clamp(
          static_cast<double>(pending.progress->overall_progress), 0.0, 1.0);
        const int bar_width_pending = std::max(8, std::min(24, cols - 12));
        const int filled_pending
          = static_cast<int>(progress_ratio * bar_width_pending);
        std::wstring pending_bar;
        pending_bar.reserve(static_cast<size_t>(bar_width_pending) + 2);
        pending_bar.push_back(L'[');
        for (int i = 0; i < bar_width_pending; ++i) {
          pending_bar.push_back(i < filled_pending ? kBarFilled : kBarEmpty);
        }
        pending_bar.push_back(L']');
        mvaddwstr(row, 4, pending_bar.c_str());
        const int percent = static_cast<int>(progress_ratio * 100.0 + 0.5);
        if (cols - (4 + bar_width_pending + 6) > 0) {
          mvprintw(row, 4 + bar_width_pending + 3, " %3d%%", percent);
        }
        ++row;
      }
    }

    refresh();
  }

} // namespace

auto BatchCommand::Name() const -> std::string_view { return "batch"; }

auto BatchCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto manifest = Option::WithKey("manifest")
                    .About("Path to the import manifest JSON")
                    .Short("m")
                    .Long("manifest")
                    .WithValue<std::string>()
                    .StoreTo(&options_.manifest_path)
                    .Build();

  auto root = Option::WithKey("root")
                .About("Root path for resolving relative sources")
                .Long("root")
                .WithValue<std::string>()
                .StoreTo(&options_.root_path)
                .Build();

  auto dry_run = Option::WithKey("dry-run")
                   .About("Validate and print jobs without executing")
                   .Long("dry-run")
                   .WithValue<bool>()
                   .StoreTo(&options_.dry_run)
                   .Build();

  auto fail_fast = Option::WithKey("fail-fast")
                     .About("Stop processing after the first failure")
                     .Long("fail-fast")
                     .WithValue<bool>()
                     .StoreTo(&options_.fail_fast)
                     .Build();

  auto verbose = Option::WithKey("verbose")
                   .About("Print progress updates")
                   .Short("v")
                   .Long("verbose")
                   .WithValue<bool>()
                   .StoreTo(&options_.verbose)
                   .Build();

  auto report = Option::WithKey("report")
                  .About("Write a JSON report (absolute or relative to cooked "
                         "root)")
                  .Long("report")
                  .WithValue<std::string>()
                  .StoreTo(&options_.report_path)
                  .Build();

  auto max_in_flight = Option::WithKey("max-in-flight")
                         .About("Limit number of in-flight jobs")
                         .Long("max-in-flight")
                         .WithValue<uint32_t>()
                         .StoreTo(&options_.max_in_flight)
                         .Build();

  auto no_tui = Option::WithKey("no-tui")
                  .About("Disable curses UI")
                  .Long("no-tui")
                  .WithValue<bool>()
                  .StoreTo(&options_.no_tui)
                  .Build();

  return CommandBuilder("batch")
    .About("Run a batch import manifest")
    .WithOption(std::move(manifest))
    .WithOption(std::move(root))
    .WithOption(std::move(dry_run))
    .WithOption(std::move(fail_fast))
    .WithOption(std::move(verbose))
    .WithOption(std::move(report))
    .WithOption(std::move(max_in_flight))
    .WithOption(std::move(no_tui));
}

auto BatchCommand::Run() -> int
{
  if (options_.manifest_path.empty()) {
    std::cerr << "ERROR: --manifest is required\n";
    return 2;
  }

  std::optional<std::filesystem::path> root_override;
  if (!options_.root_path.empty()) {
    root_override = std::filesystem::path(options_.root_path);
  }

  const auto manifest = ImportManifestLoader::Load(
    std::filesystem::path(options_.manifest_path), root_override, std::cerr);
  if (!manifest.has_value()) {
    return 2;
  }

  int failures = 0;
  std::vector<PreparedJob> jobs;
  jobs.reserve(manifest->jobs.size());

  for (const auto& job : manifest->jobs) {
    if (job.job_type != "texture") {
      std::cerr << "ERROR: unsupported job type: " << job.job_type << "\n";
      ++failures;
      if (options_.fail_fast) {
        break;
      }
      continue;
    }

    auto settings = job.texture;
    if (ShouldOverride(options_.verbose, settings.verbose)) {
      settings.verbose = true;
    }

    if (options_.dry_run) {
      const auto request = BuildTextureRequest(settings, std::cerr);
      if (!request.has_value()) {
        ++failures;
        if (options_.fail_fast) {
          break;
        }
        continue;
      }
      std::cout << "DRY-RUN: texture source=" << settings.source_path << "\n";
      continue;
    }

    const auto request = BuildTextureRequest(settings, std::cerr);
    if (!request.has_value()) {
      ++failures;
      if (options_.fail_fast) {
        break;
      }
      continue;
    }

    jobs.push_back({
      .request = *request,
      .verbose = settings.verbose,
      .source_path = settings.source_path,
    });
  }

  if (options_.dry_run || jobs.empty()) {
    return failures == 0 ? 0 : 2;
  }

  std::mutex mutex;
  std::mutex output_mutex;
  std::deque<UiEvent> recent_events;
  constexpr size_t kMaxEvents = 12;
  std::condition_variable cv;
  std::atomic<bool> ui_dirty { true };
  size_t remaining = jobs.size();
  size_t completed = 0;
  const size_t total = jobs.size();
  size_t in_flight = 0;
  const size_t max_in_flight = options_.max_in_flight > 0
    ? static_cast<size_t>(options_.max_in_flight)
    : std::max<size_t>(1, std::thread::hardware_concurrency());

  std::vector<std::optional<ImportReport>> reports(jobs.size());
  std::vector<std::optional<ImportJobId>> job_ids(jobs.size());
  std::vector<std::optional<ImportProgress>> progress_states(jobs.size());
  std::vector<std::optional<std::chrono::steady_clock::time_point>>
    progress_times(jobs.size());
  std::vector<std::optional<std::chrono::steady_clock::time_point>>
    submit_times(jobs.size());
  AsyncImportService service;
  ScopeGuard stop_guard([&]() noexcept { service.Stop(); });

  bool tui_enabled = !options_.no_tui;
  if (tui_enabled) {
    std::setlocale(LC_ALL, "");
    if (initscr() == nullptr) {
      tui_enabled = false;
    } else {
      cbreak();
      noecho();
      keypad(stdscr, TRUE);
      nodelay(stdscr, TRUE);
      curs_set(0);
    }
  }

  const auto BuildSnapshot = [&]() -> UiSnapshot {
    UiSnapshot snapshot {};
    std::lock_guard lock(mutex);
    snapshot.completed = completed;
    snapshot.total = total;
    snapshot.in_flight = in_flight;
    snapshot.remaining = remaining;
    snapshot.failures = failures;
    snapshot.pending = BuildPendingSnapshot(reports, jobs, job_ids,
      progress_states, progress_times, submit_times, 10);
    snapshot.failed_lines = BuildFailedLines(reports, jobs, 16);
    snapshot.recent = recent_events;
    return snapshot;
  };

  const auto RenderNow
    = [&](const std::chrono::steady_clock::time_point start) -> void {
    if (!tui_enabled) {
      return;
    }
    auto snapshot = BuildSnapshot();
    snapshot.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - start);
    RenderTui(snapshot);
  };

  const auto wait_start = std::chrono::steady_clock::now();
  RenderNow(wait_start);

  for (size_t index = 0; index < jobs.size(); ++index) {
    const auto& job = jobs[index];

    const auto on_complete
      = [&](const size_t job_index) -> ImportCompletionCallback {
      return [&, job_index](ImportJobId, const ImportReport& report) {
        size_t completed_snapshot = 0;
        std::optional<ImportJobId> job_id_snapshot;
        {
          std::lock_guard lock(mutex);
          reports[job_index] = report;
          job_id_snapshot = job_ids[job_index];
          if (!report.success) {
            ++failures;
          }
          if (remaining > 0) {
            --remaining;
          }
          if (in_flight > 0) {
            --in_flight;
          }
          ++completed;
          completed_snapshot = completed;
        }
        cv.notify_one();

        {
          std::ostringstream line;
          line << "completed job=" << DisplayJobNumber(job_index) << " id="
               << (job_id_snapshot.has_value()
                      ? std::to_string(*job_id_snapshot)
                      : std::string("n/a"))
               << " (" << completed_snapshot << "/" << total << ")"
               << " source=" << jobs[job_index].source_path
               << " success=" << (report.success ? "true" : "false");
          EmitEvent(output_mutex, recent_events,
            { .text = line.str(), .is_error = !report.success }, kMaxEvents,
            tui_enabled);
        }
        ui_dirty.store(true, std::memory_order_release);

        if (!report.success) {
          if (tui_enabled) {
            for (const auto& diag : report.diagnostics) {
              std::ostringstream line;
              line << "job=" << DisplayJobNumber(job_index) << " " << diag.code
                   << ": " << diag.message;
              EmitEvent(output_mutex, recent_events,
                { .text = line.str(), .is_error = true }, kMaxEvents,
                tui_enabled);
            }
          } else {
            PrintDiagnostics(report, job_index, jobs[job_index].source_path);
          }
        }
      };
    }(index);

    const auto on_progress
      = [&](const size_t job_index) -> ImportProgressCallback {
      return [&, job_index](const ImportProgress& progress) {
        {
          std::lock_guard lock(mutex);
          progress_states[job_index] = progress;
          progress_times[job_index] = std::chrono::steady_clock::now();
        }
        ui_dirty.store(true, std::memory_order_release);
        cv.notify_one();
        if (!jobs[job_index].verbose) {
          return;
        }
        if (!tui_enabled) {
          std::ostringstream line;
          line << "job=" << DisplayJobNumber(job_index)
               << " phase=" << static_cast<uint32_t>(progress.phase)
               << " overall=" << progress.overall_progress;
          EmitEvent(output_mutex, recent_events,
            { .text = line.str(), .is_error = false }, kMaxEvents, tui_enabled);
        }
      };
    }(index);

    ImportJobId job_id = kInvalidJobId;
    while (job_id == kInvalidJobId) {
      job_id = service.SubmitImport(job.request, on_complete, on_progress);
      if (job_id != kInvalidJobId) {
        break;
      }

      if (!service.IsAcceptingJobs()) {
        break;
      }

      size_t completed_snapshot = 0;
      {
        std::unique_lock lock(mutex);
        completed_snapshot = completed;
        cv.wait(lock, [&] { return completed != completed_snapshot; });
      }
    }
    if (job_id == kInvalidJobId) {
      {
        std::ostringstream line;
        line << "ERROR: failed to submit import job: " << job.source_path;
        EmitEvent(output_mutex, recent_events,
          { .text = line.str(), .is_error = true }, kMaxEvents, tui_enabled);
      }
      ui_dirty.store(true, std::memory_order_release);
      ++failures;
      size_t completed_snapshot = 0;
      {
        std::lock_guard lock(mutex);
        if (remaining > 0) {
          --remaining;
        }
        if (in_flight > 0) {
          --in_flight;
        }
        ++completed;
        completed_snapshot = completed;
        if (options_.fail_fast) {
          const auto pending_unsubmitted = jobs.size() - index - 1;
          if (remaining >= pending_unsubmitted) {
            remaining -= pending_unsubmitted;
          } else {
            remaining = 0;
          }
        }
      }
      {
        std::ostringstream line;
        line << "completed job=" << DisplayJobNumber(index) << " id=n/a"
             << " (" << completed_snapshot << "/" << total << ")"
             << " source=" << job.source_path << " success=false";
        EmitEvent(output_mutex, recent_events,
          { .text = line.str(), .is_error = true }, kMaxEvents, tui_enabled);
      }
      if (options_.fail_fast) {
        cv.notify_one();
        break;
      }
    } else {
      {
        std::lock_guard lock(mutex);
        job_ids[index] = job_id;
        submit_times[index] = std::chrono::steady_clock::now();
        ++in_flight;
      }
      {
        std::ostringstream line;
        line << "submitted job=" << DisplayJobNumber(index) << " id=" << job_id
             << " source=" << job.source_path;
        EmitEvent(output_mutex, recent_events,
          { .text = line.str(), .is_error = false }, kMaxEvents, tui_enabled);
      }
      ui_dirty.store(true, std::memory_order_release);
      cv.notify_one();
      cv.notify_one();
    }

    while (true) {
      {
        std::unique_lock lock(mutex);
        if (cv.wait_for(lock, std::chrono::milliseconds(200),
              [&] { return in_flight < max_in_flight || remaining == 0; })) {
          break;
        }
      }

      if (tui_enabled && ui_dirty.exchange(false)) {
        auto snapshot = BuildSnapshot();
        snapshot.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - wait_start);
        RenderTui(snapshot);
      }
    }
  }

  UiSnapshot final_snapshot {};
  while (true) {
    UiSnapshot snapshot {};

    {
      std::unique_lock lock(mutex);
      if (remaining == 0 && in_flight == 0 && completed == total) {
        snapshot.completed = completed;
        snapshot.total = total;
        snapshot.in_flight = in_flight;
        snapshot.remaining = remaining;
        snapshot.failures = failures;
        snapshot.failed_lines = BuildFailedLines(reports, jobs, 16);
        snapshot.recent = recent_events;
        final_snapshot = snapshot;
        break;
      }
      cv.wait_for(lock, std::chrono::milliseconds(200));

      snapshot.completed = completed;
      snapshot.total = total;
      snapshot.in_flight = in_flight;
      snapshot.remaining = remaining;
      snapshot.failures = failures;
      snapshot.pending = BuildPendingSnapshot(reports, jobs, job_ids,
        progress_states, progress_times, submit_times, 10);
      snapshot.failed_lines = BuildFailedLines(reports, jobs, 16);
      snapshot.recent = recent_events;
    }

    snapshot.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - wait_start);

    if (tui_enabled) {
      RenderTui(snapshot);
    } else {
      std::ostringstream block;
      block << "progress " << snapshot.completed << "/" << snapshot.total
            << " pending=" << snapshot.remaining
            << " elapsed=" << snapshot.elapsed.count() << "s\n";
      EmitEvent(output_mutex, recent_events,
        { .text = block.str(), .is_error = false }, kMaxEvents, tui_enabled);
    }
  }

  final_snapshot.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::steady_clock::now() - wait_start);
  final_snapshot.pending.clear();
  if (tui_enabled) {
    RenderTui(final_snapshot);
  }

  if (tui_enabled) {
    nodelay(stdscr, FALSE);
    mvprintw(LINES - 1, 0, "Press any key to exit...");
    refresh();
    getch();
    endwin();
  }

  if (!options_.report_path.empty()) {
    const auto cooked_root = ResolveCookedRootForReport(jobs, reports);
    const auto resolved_path
      = ResolveReportPath(options_.report_path, cooked_root, std::cerr);
    if (!resolved_path.has_value()) {
      return 2;
    }

    const auto elapsed_ms
      = std::chrono::duration<double, std::milli>(final_snapshot.elapsed)
          .count();
    ordered_json jobs_json = ordered_json::array();
    for (size_t index = 0; index < jobs.size(); ++index) {
      const auto& job = jobs[index];
      const auto& report = reports[index];
      const bool success = report.has_value() && report->success;
      ordered_json telemetry = nullptr;
      if (report.has_value()) {
        telemetry = BuildTelemetryJson(report->telemetry);
      }
      jobs_json.push_back({
        { "index", DisplayJobNumber(index) },
        { "source", job.source_path },
        { "success", success },
        { "telemetry", telemetry },
      });
    }

    ordered_json payload = ordered_json::object();
    payload["summary"] = {
      { "jobs", jobs.size() },
      { "succeeded", jobs.size() - static_cast<size_t>(failures) },
      { "failed", static_cast<size_t>(failures) },
      { "total_time_ms", elapsed_ms },
      { "cooked_root",
        cooked_root.has_value() ? cooked_root->string() : std::string() },
    };
    payload["jobs"] = std::move(jobs_json);

    if (!WriteJsonReport(payload, *resolved_path, std::cerr)) {
      return 2;
    }
  }

  return failures == 0 ? 0 : 2;
}

} // namespace oxygen::content::import::tool
