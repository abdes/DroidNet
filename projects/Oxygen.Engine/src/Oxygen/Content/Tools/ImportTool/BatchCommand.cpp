//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <clocale>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportManifest.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Content/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Content/Import/SceneImportSettings.h>
#include <Oxygen/Content/Import/TextureImportSettings.h>
#include <Oxygen/Content/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Content/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Content/Tools/ImportTool/ReportJson.h>
#include <Oxygen/Content/Tools/ImportTool/UI/BatchViewModel.h>
#include <Oxygen/Content/Tools/ImportTool/UI/Screens/BatchImportScreen.h>
#include <Oxygen/OxCo/Detail/ScopeGuard.h>

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::ordered_json;
  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;
  using oxygen::co::detail::ScopeGuard;

  // Helper structs for logic (kept for now, or moved to ViewModel later)
  struct PreparedJob {
    ImportRequest request;
    bool verbose = false;
    std::string source_path;
  };

  auto DisplayJobNumber(const size_t job_index) -> size_t
  {
    return job_index + 1;
  }

  auto PrintDiagnostics(const ImportReport& report, const size_t job_index,
    std::string_view source_path,
    const oxygen::observer_ptr<IMessageWriter>& writer) -> void
  {
    if (report.success) {
      return;
    }

    writer->Error(fmt::format("ERROR: import failed (job={}, source={})",
      DisplayJobNumber(job_index), source_path));
    for (const auto& diag : report.diagnostics) {
      writer->Error(fmt::format("- {}: {}", diag.code, diag.message));
    }
  }

  // --- Logic Helpers (Restored) ---

  constexpr auto PhaseCount() -> size_t
  {
    return static_cast<size_t>(ImportPhase::kFailed) + 1U;
  }

  using WorkerTotals = std::array<uint32_t, 7>;

  auto BuildWorkerTotals(const ImportManifest& manifest) -> WorkerTotals
  {
    const ImportConcurrency concurrency
      = manifest.concurrency.value_or(ImportConcurrency {});
    return {
      concurrency.texture.workers,
      concurrency.buffer.workers,
      concurrency.material.workers,
      concurrency.mesh_build.workers,
      concurrency.geometry.workers,
      concurrency.scene.workers,
      0U,
    };
  }

  auto BuildWorkerUtilizationViews(const WorkerTotals& totals)
    -> std::vector<WorkerUtilizationView>
  {
    const std::array<std::string_view, 7> kinds {
      "Texture",
      "Buffer",
      "Material",
      "MeshBuild",
      "Geometry",
      "Scene",
      "Audio",
    };

    std::vector<WorkerUtilizationView> result;
    result.reserve(kinds.size());
    for (size_t index = 0; index < kinds.size(); ++index) {
      WorkerUtilizationView entry {};
      entry.kind = std::string(kinds[index]);
      entry.total = totals[index];
      entry.queue_load = 0.0f;
      result.push_back(entry);
    }
    return result;
  }

  auto WorkerKindIndex(std::string_view kind) -> std::optional<size_t>
  {
    if (kind == "Texture") {
      return 0U;
    }
    if (kind == "Buffer") {
      return 1U;
    }
    if (kind == "Material") {
      return 2U;
    }
    if (kind == "MeshBuild") {
      return 3U;
    }
    if (kind == "Geometry") {
      return 4U;
    }
    if (kind == "Scene") {
      return 5U;
    }
    if (kind == "Audio") {
      return 6U;
    }
    return std::nullopt;
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

  auto IsCanceledReport(const ImportReport& report) -> bool
  {
    for (const auto& diag : report.diagnostics) {
      if (diag.code == "import.canceled") {
        return true;
      }
    }
    return false;
  }

  auto JobStatusFromReport(const std::optional<ImportReport>& report)
    -> std::string_view
  {
    if (!report.has_value()) {
      return "Not Started";
    }
    if (report->success) {
      return "Success";
    }
    if (IsCanceledReport(*report)) {
      return "Canceled";
    }
    return "Failed";
  }

  struct BatchSummaryCounts {
    size_t succeeded = 0;
    size_t failed = 0;
    size_t canceled = 0;
    size_t not_started = 0;
  };

  auto BuildBatchSummary(
    const std::vector<std::optional<ImportReport>>& reports,
    const size_t total_jobs) -> BatchSummaryCounts
  {
    BatchSummaryCounts counts {};
    for (const auto& report : reports) {
      const auto status = JobStatusFromReport(report);
      if (status == "Success") {
        ++counts.succeeded;
      } else if (status == "Canceled") {
        ++counts.canceled;
      } else if (status == "Not Started") {
        ++counts.not_started;
      }
    }
    if (counts.succeeded + counts.canceled + counts.not_started <= total_jobs) {
      counts.failed
        = total_jobs - counts.succeeded - counts.canceled - counts.not_started;
    }
    return counts;
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

  auto quiet = Option::WithKey("quiet")
                 .About("Suppress non-error output")
                 .Short("q")
                 .Long("quiet")
                 .WithValue<bool>()
                 .StoreTo(&options_.quiet)
                 .Build();

  auto report = Option::WithKey("report")
                  .About("Write a JSON report (absolute or relative to cooked "
                         "root)")
                  .Long("report")
                  .WithValue<std::string>()
                  .StoreTo(&options_.report_path)
                  .Build();

  auto max_in_flight = Option::WithKey("max-in-flight-jobs")
                         .About("Maximum number of in-flight jobs")
                         .Long("max-in-flight-jobs")
                         .WithValue<uint32_t>()
                         .StoreTo(&options_.max_in_flight_jobs)
                         .CallOnFinalValue([this](const uint32_t&) {
                           options_.max_in_flight_jobs_set = true;
                         })
                         .Build();

  return CommandBuilder("batch")
    .About("Run a batch import manifest")
    .WithOption(std::move(manifest))
    .WithOption(std::move(root))
    .WithOption(std::move(dry_run))
    .WithOption(std::move(fail_fast))
    .WithOption(std::move(quiet))
    .WithOption(std::move(report))
    .WithOption(std::move(max_in_flight));
}

auto BatchCommand::PrepareImportServiceConfig()
  -> std::expected<AsyncImportService::Config, std::error_code>
{
  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto writer = global_options_->writer;
  DCHECK_NOTNULL_F(
    global_options_->import_service, "Import service must be set by main");
  auto import_service = global_options_->import_service;

  if (options_.manifest_path.empty()) {
    writer->Error("ERROR: --manifest is required");
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  std::optional<std::filesystem::path> root_override;
  if (!options_.root_path.empty()) {
    root_override = std::filesystem::path(options_.root_path);
  }

  std::optional<ImportManifest> manifest;
  {
    std::ostringstream err;
    manifest = ImportManifest::Load(
      std::filesystem::path(options_.manifest_path), root_override, err);
    if (!manifest.has_value()) {
      const auto msg = err.str();
      if (!msg.empty()) {
        writer->Error(msg);
      }
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }

  prepared_manifest_ = manifest;

  AsyncImportService::Config service_config {};
  if (manifest->thread_pool_size.has_value()) {
    service_config.thread_pool_size = *manifest->thread_pool_size;
  }
  if (manifest->max_in_flight_jobs.has_value()) {
    service_config.max_in_flight_jobs = *manifest->max_in_flight_jobs;
  }
  if (options_.max_in_flight_jobs_set) {
    service_config.max_in_flight_jobs = options_.max_in_flight_jobs;
  }
  if (manifest->concurrency.has_value()) {
    service_config.concurrency = *manifest->concurrency;
  }

  return service_config;
}

auto BatchCommand::Run() -> std::expected<void, std::error_code>
{
  // 1. Process Options
  if (global_options_ != nullptr) {
    if (!options_.fail_fast && global_options_->fail_fast) {
      options_.fail_fast = true;
    }
    if (!options_.quiet && global_options_->quiet) {
      options_.quiet = true;
    }
    // TUI control is global-only; respect the global --no-tui setting
  }

  // Prepare a MessageWriter for console output. The global writer MUST be
  // provided by main; never create a local writer.
  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto writer = global_options_->writer;
  DCHECK_NOTNULL_F(
    global_options_->import_service, "Import service must be set by main");
  auto import_service = global_options_->import_service;

  if (options_.manifest_path.empty()) {
    writer->Error("ERROR: --manifest is required");
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  // 2. Load Manifest
  std::optional<ImportManifest> manifest;
  if (prepared_manifest_.has_value()) {
    manifest = std::move(prepared_manifest_);
  } else {
    std::optional<std::filesystem::path> root_override;
    if (!options_.root_path.empty()) {
      root_override = std::filesystem::path(options_.root_path);
    }

    std::ostringstream err;
    manifest = ImportManifest::Load(
      std::filesystem::path(options_.manifest_path), root_override, err);
    if (!manifest.has_value()) {
      const auto msg = err.str();
      if (!msg.empty()) {
        writer->Error(msg);
      }
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }

  // 3. Prepare Jobs
  if (service_config_override_ != nullptr && concurrency_override_set_) {
    manifest->concurrency = service_config_override_->concurrency;
  }
  const auto worker_totals = BuildWorkerTotals(*manifest);
  int validation_failures = 0;
  bool unsupported_seen = false;
  std::vector<PreparedJob> jobs;
  jobs.reserve(manifest->jobs.size());

  for (const auto& job : manifest->jobs) {
    if (job.job_type != "texture" && job.job_type != "fbx"
      && job.job_type != "gltf") {
      writer->Error(
        fmt::format("ERROR: unsupported job type: {}", job.job_type));
      unsupported_seen = true;
      ++validation_failures;
      if (options_.fail_fast) {
        return std::unexpected(std::make_error_code(std::errc::not_supported));
      }
      continue;
    }

    if (job.job_type == "texture") {
      auto settings = job.texture;
      if (global_options_ != nullptr && settings.cooked_root.empty()) {
        settings.cooked_root = global_options_->cooked_root;
      }
      if (options_.quiet) {
        settings.verbose = false;
      }

      std::optional<ImportRequest> request;
      {
        std::ostringstream err;
        request = internal::BuildTextureRequest(settings, err);
        if (!request) {
          const auto msg = err.str();
          if (!msg.empty()) {
            writer->Error(msg);
          }
          ++validation_failures;
          if (options_.fail_fast) {
            break;
          }
          continue;
        }
      }

      if (options_.dry_run) {
        writer->Info(fmt::format("DRY-RUN: texture {}", settings.source_path));
        continue;
      }

      jobs.push_back({ .request = *request,
        .verbose = settings.verbose,
        .source_path = settings.source_path });
      continue;
    }

    // Scene Imports
    SceneImportSettings settings = job.job_type == "fbx" ? job.fbx : job.gltf;
    if (global_options_ != nullptr && settings.cooked_root.empty()) {
      settings.cooked_root = global_options_->cooked_root;
    }
    if (options_.quiet) {
      settings.verbose = false;
    }

    const auto expected_format
      = job.job_type == "fbx" ? ImportFormat::kFbx : ImportFormat::kGltf;
    std::optional<ImportRequest> request;
    {
      std::ostringstream err;
      request = internal::BuildSceneRequest(settings, expected_format, err);
      if (!request) {
        const auto msg = err.str();
        if (!msg.empty()) {
          writer->Error(msg);
        }
        ++validation_failures;
        if (options_.fail_fast) {
          break;
        }
        continue;
      }
    }

    if (options_.dry_run) {
      writer->Info(
        fmt::format("DRY-RUN: {} {}", job.job_type, settings.source_path));
      continue;
    }

    jobs.push_back({ .request = *request,
      .verbose = settings.verbose,
      .source_path = settings.source_path });
  }

  if (options_.dry_run || jobs.empty()) {
    if (validation_failures == 0) {
      return {};
    }
    if (unsupported_seen) {
      return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  if (validation_failures > 0 && options_.fail_fast) {
    if (unsupported_seen) {
      return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  // 4. Execution Logic (Worker)
  struct SharedContext {
    std::mutex mutex;
    std::condition_variable completed_cv;
    BatchViewModel state;
    bool completed = false;
    int exit_code = 0;
    std::vector<std::optional<ImportReport>> reports;
  };
  auto common_context = std::make_shared<SharedContext>();
  common_context->state.manifest_path = options_.manifest_path;
  common_context->state.total = jobs.size();
  common_context->state.remaining = jobs.size();
  common_context->state.in_flight = 0;
  common_context->state.progress = 0.0f;
  common_context->state.completed_run = false;
  common_context->state.worker_utilization
    = BuildWorkerUtilizationViews(worker_totals);
  common_context->reports.resize(jobs.size());

  std::vector<JobProgressTrace> progress_traces(jobs.size());
  std::vector<std::optional<std::chrono::steady_clock::time_point>>
    submit_times(jobs.size());

  auto worker_thread = std::jthread([this, &jobs, common_context, writer,
                                      import_service, &progress_traces,
                                      &submit_times,
                                      worker_totals](std::stop_token st) {
    DCHECK_NOTNULL_F(import_service, "Import service must be set by main");

    size_t submitted = 0;
    size_t completed = 0;
    size_t failures = 0;
    size_t in_flight = 0;
    std::vector<std::optional<ImportJobId>> job_ids(jobs.size());
    std::vector<ActiveJobView> job_views(jobs.size());
    std::vector<bool> job_active(jobs.size(), false);

    std::array<uint32_t, 7> outstanding_items { 0U, 0U, 0U, 0U, 0U, 0U, 0U };
    std::array<float, 7> queue_loads {
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
    };
    std::vector<std::array<uint32_t, 7>> per_job_outstanding(
      jobs.size(), { 0U, 0U, 0U, 0U, 0U, 0U, 0U });
    std::vector<std::unordered_set<std::string>> items_started(jobs.size());
    std::vector<std::unordered_set<std::string>> items_finished(jobs.size());

    auto PendingCount = [&](const size_t total, const size_t completed_count,
                          const size_t in_flight_count) -> size_t {
      if (completed_count + in_flight_count >= total) {
        return 0U;
      }
      return total - completed_count - in_flight_count;
    };

    auto start_time = std::chrono::steady_clock::now();

    auto UpdateActiveJobs = [&]() {
      common_context->state.active_jobs.clear();
      for (size_t index = 0; index < job_views.size(); ++index) {
        if (job_active[index]) {
          common_context->state.active_jobs.push_back(job_views[index]);
        }
      }
    };

    auto UpdateWorkerUtilization = [&]() {
      common_context->state.worker_utilization
        = BuildWorkerUtilizationViews(worker_totals);
      for (auto& entry : common_context->state.worker_utilization) {
        const auto index = WorkerKindIndex(entry.kind);
        if (index.has_value()) {
          entry.active = std::min(outstanding_items[*index], entry.total);
          entry.queue_load = queue_loads[*index];
        }
      }
    };

    bool shutdown_requested = false;
    auto RequestShutdown = [&]() {
      if (shutdown_requested) {
        return;
      }
      shutdown_requested = true;
      import_service->CancelAll();
      import_service->RequestShutdown();
    };

    while (!st.stop_requested() && (submitted < jobs.size() || in_flight > 0)) {
      if (!import_service->IsAcceptingJobs()) {
        RequestShutdown();
      }

      if (shutdown_requested) {
        break;
      }

      while (!shutdown_requested && submitted < jobs.size()) {
        if (st.stop_requested()) {
          RequestShutdown();
          break;
        }
        auto& job = jobs[submitted];

        auto on_complete = [&, submitted](
                             ImportJobId id, const ImportReport& report) {
          std::scoped_lock lock(common_context->mutex);

          if (job_ids[submitted].has_value() && id != *job_ids[submitted]) {
            const auto u_expected = job_ids[submitted]->get();
            const auto u_actual = id.get();
            common_context->state.recent_logs.push_back(
              fmt::format("Job {} id mismatch (expected {}, got {})",
                DisplayJobNumber(submitted), u_expected, u_actual));
          }

          job_views[submitted].progress = 1.0f;
          job_views[submitted].status = report.success ? "Completed" : "Failed";
          job_views[submitted].item_event = "";
          items_started[submitted].clear();
          items_finished[submitted].clear();
          job_views[submitted].items_completed = 0U;
          job_views[submitted].items_total = 0U;
          job_active[submitted] = false;
          auto& job_outstanding = per_job_outstanding[submitted];
          for (size_t index = 0; index < job_outstanding.size(); ++index) {
            const auto pending = job_outstanding[index];
            if (pending == 0U) {
              continue;
            }
            if (outstanding_items[index] >= pending) {
              outstanding_items[index] -= pending;
            } else {
              outstanding_items[index] = 0U;
            }
            job_outstanding[index] = 0U;
          }

          common_context->reports[submitted] = report;
          if (!report.success) {
            failures++;
            common_context->state.failures = failures;
            common_context->exit_code = 2; // Fail code

            for (const auto& diag : report.diagnostics) {
              common_context->state.recent_logs.push_back(
                fmt::format("✖ Job {} Failed: {}: {}",
                  DisplayJobNumber(submitted), diag.code, diag.message));
            }
          } else {
            common_context->state.recent_logs.push_back(
              fmt::format("✔ Job {} Completed", DisplayJobNumber(submitted)));
            writer->Report(
              fmt::format("Job {} Completed", DisplayJobNumber(submitted)));
          }

          // Cap logs
          if (common_context->state.recent_logs.size() > 50) {
            common_context->state.recent_logs.erase(
              common_context->state.recent_logs.begin(),
              common_context->state.recent_logs.end() - 50);
          }

          completed++;
          in_flight--;

          common_context->state.completed = completed;
          common_context->state.in_flight = in_flight;
          common_context->state.remaining
            = PendingCount(jobs.size(), completed, in_flight);

          if (!jobs.empty()) {
            common_context->state.progress
              = static_cast<float>(completed) / static_cast<float>(jobs.size());
          }

          UpdateActiveJobs();
          UpdateWorkerUtilization();
        };

        auto on_progress = [&, submitted](const ProgressEvent& progress) {
          std::scoped_lock lock(common_context->mutex);
          if (progress.header.kind == ProgressEventKind::kPhaseUpdate) {
            return;
          }
          if (progress.header.kind == ProgressEventKind::kJobFinished) {
            return;
          }
          {
            auto EventLabel = [](const ProgressEventKind kind) -> std::string {
              switch (kind) {
              case ProgressEventKind::kItemStarted:
                return "Started";
              case ProgressEventKind::kItemFinished:
                return "Finished";
              case ProgressEventKind::kItemCollected:
                return "Collected";
              case ProgressEventKind::kPhaseUpdate:
                return "Phase";
              case ProgressEventKind::kJobStarted:
                return "Job Started";
              case ProgressEventKind::kJobFinished:
                return "Job Finished";
              }
              return "Event";
            };
            auto PhaseCode = [](const ImportPhase phase) -> char {
              switch (phase) {
              case ImportPhase::kPending:
                return 'P';
              case ImportPhase::kLoading:
                return 'L';
              case ImportPhase::kPlanning:
                return 'N';
              case ImportPhase::kWorking:
                return 'W';
              case ImportPhase::kFinalizing:
                return 'F';
              case ImportPhase::kComplete:
                return 'C';
              case ImportPhase::kCancelled:
                return 'X';
              case ImportPhase::kFailed:
                return 'E';
              }
              return '?';
            };
            std::string event_label = EventLabel(progress.header.kind);
            if (const auto* item = GetItemProgress(progress)) {
              if (!item->item_kind.empty()) {
                event_label = fmt::format(
                  "{} {}", item->item_kind, EventLabel(progress.header.kind));
              }
            }
            std::string line
              = fmt::format("Job {}-{} {}", DisplayJobNumber(submitted),
                PhaseCode(progress.header.phase), event_label);
            if (const auto* item = GetItemProgress(progress)) {
              if (!item->item_name.empty()) {
                line.append(" ");
                line.append(item->item_name);
              }
              if (progress.header.kind == ProgressEventKind::kItemCollected) {
                line.append(fmt::format(" load={:.2f}", item->queue_load));
              }
            }
            writer->Progress(line);
            common_context->state.recent_logs.push_back(std::move(line));
            if (common_context->state.recent_logs.size() > 50) {
              common_context->state.recent_logs.erase(
                common_context->state.recent_logs.begin(),
                common_context->state.recent_logs.end() - 50);
            }
          }
          if (progress.header.kind == ProgressEventKind::kJobStarted) {
            submit_times[submitted] = std::chrono::steady_clock::now();
            job_views[submitted].status = "Running";
          }

          job_views[submitted].progress = progress.header.overall_progress;
          job_views[submitted].status
            = std::string(nostd::to_string(progress.header.phase));

          if (const auto* item = GetItemProgress(progress)) {
            if (progress.header.kind == ProgressEventKind::kItemCollected) {
              if (!item->item_kind.empty()) {
                const auto index = WorkerKindIndex(item->item_kind);
                if (index.has_value()) {
                  DCHECK_F(item->queue_load >= 0.0f && item->queue_load <= 1.0f,
                    "Item collection queue load is out of range: {}",
                    item->queue_load);
                  queue_loads[*index] = item->queue_load;
                }
              }
            } else {
              if (!item->item_kind.empty()) {
                job_views[submitted].item_kind = item->item_kind;
              }
              if (!item->item_name.empty()) {
                job_views[submitted].item_name = item->item_name;
              }
              if (progress.header.kind == ProgressEventKind::kItemStarted) {
                job_views[submitted].item_event = "started";
              } else if (progress.header.kind
                == ProgressEventKind::kItemFinished) {
                job_views[submitted].item_event = "finished";
              }

              if (!item->item_kind.empty() || !item->item_name.empty()) {
                std::string key;
                if (!item->item_kind.empty()) {
                  key = item->item_kind;
                }
                if (!item->item_name.empty()) {
                  if (!key.empty()) {
                    key.append(":");
                  }
                  key.append(item->item_name);
                }
                if (!key.empty()) {
                  if (progress.header.kind == ProgressEventKind::kItemStarted) {
                    items_started[submitted].insert(key);
                  } else if (progress.header.kind
                    == ProgressEventKind::kItemFinished) {
                    items_finished[submitted].insert(key);
                  }
                  job_views[submitted].items_total
                    = static_cast<uint32_t>(items_started[submitted].size());
                  job_views[submitted].items_completed
                    = static_cast<uint32_t>(items_finished[submitted].size());
                }
              }

              if (!item->item_kind.empty()) {
                const auto index = WorkerKindIndex(item->item_kind);
                if (index.has_value()) {
                  auto& active = outstanding_items[*index];
                  auto& per_job = per_job_outstanding[submitted][*index];
                  if (progress.header.kind == ProgressEventKind::kItemStarted) {
                    ++active;
                    ++per_job;
                  } else if (progress.header.kind
                    == ProgressEventKind::kItemFinished) {
                    if (active > 0U) {
                      --active;
                    }
                    if (per_job > 0U) {
                      --per_job;
                    }
                  }
                }
              }
            }
          }

          UpdateActiveJobs();
          UpdateWorkerUtilization();
        };

        if (auto id = import_service->SubmitImport(
              job.request, on_complete, on_progress)) {
          job_ids[submitted] = *id;
          job_views[submitted].id = std::to_string(DisplayJobNumber(submitted));
          job_views[submitted].source = job.source_path;
          job_views[submitted].status = "Queued";
          job_views[submitted].progress = 0.0f;
          job_views[submitted].items_completed = 0U;
          job_views[submitted].items_total = 0U;
          job_active[submitted] = true;
          submitted++;
          in_flight++;
        } else {
          std::scoped_lock lock(common_context->mutex);
          common_context->state.recent_logs.push_back(
            fmt::format("Backpressure: delaying submission of job {}",
              DisplayJobNumber(submitted)));
          break;
        }

        {
          std::scoped_lock lock(common_context->mutex);
          common_context->state.in_flight = in_flight;
          common_context->state.remaining
            = PendingCount(jobs.size(), completed, in_flight);
          UpdateActiveJobs();
          UpdateWorkerUtilization();
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      auto now = std::chrono::steady_clock::now();
      {
        std::scoped_lock lock(common_context->mutex);
        common_context->state.elapsed
          = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
      }
    }

    import_service->Stop();

    {
      std::scoped_lock lock(common_context->mutex);
      common_context->completed = true;
      common_context->state.remaining = 0;
      common_context->state.in_flight = 0;
      common_context->state.progress = 1.0f;
      common_context->state.active_jobs.clear();
      common_context->state.completed_run = true;
      common_context->state.worker_utilization.clear();
    }
    common_context->completed_cv.notify_all();
  });

  // 5. Run TUI or Headless
  if (!(global_options_ != nullptr && global_options_->no_tui)) {
    // TUI mode: the writer is muted by main to avoid console output.
    BatchImportScreen screen;
    screen.SetDataProvider([common_context]() {
      std::scoped_lock lock(common_context->mutex);
      return common_context->state;
    });
    screen.Run();
  } else {
    std::unique_lock lock(common_context->mutex);
    common_context->completed_cv.wait(
      lock, [&]() { return common_context->completed; });
  }

  std::optional<std::error_code> deferred_error;

  // 6. Report Generation
  std::optional<std::filesystem::path> final_report_path;
  const auto counts = BuildBatchSummary(common_context->reports, jobs.size());
  if (!options_.report_path.empty()) {
    const auto cooked_root
      = ResolveCookedRootForReport(jobs, common_context->reports);
    const auto resolved_path
      = ResolveReportPath(options_.report_path, cooked_root, std::cerr);

    if (resolved_path.has_value()) {
      const auto elapsed_ms = std::chrono::duration<double, std::milli>(
        common_context->state.elapsed)
                                .count();
      ordered_json jobs_json = ordered_json::array();

      for (size_t index = 0; index < jobs.size(); ++index) {
        const auto& job = jobs[index];
        const auto& report = common_context->reports[index];
        const bool success = report.has_value() && report->success;
        const auto status = JobStatusFromReport(report);
        ordered_json telemetry = nullptr;
        if (report.has_value()) {
          telemetry = BuildTelemetryJson(report->telemetry);
        }
        // Note: progress_traces is largely empty in this simplified worker, but
        // we pass it anyway
        ordered_json progress_json
          = nullptr; // BuildProgressJson(progress_traces[index], ...);

        jobs_json.push_back({
          { "index", DisplayJobNumber(index) },
          { "source", job.source_path },
          { "success", success },
          { "status", status },
          { "telemetry", telemetry },
          { "progress", progress_json },
        });
      }

      ordered_json payload = ordered_json::object();
      payload["summary"] = {
        { "jobs", jobs.size() },
        { "succeeded", counts.succeeded },
        { "failed", counts.failed },
        { "canceled", counts.canceled },
        { "not_started", counts.not_started },
        { "total_time_ms", elapsed_ms },
        { "cooked_root",
          cooked_root.has_value() ? cooked_root->string() : std::string() },
      };
      payload["jobs"] = std::move(jobs_json);

      if (!WriteJsonReport(payload, *resolved_path, std::cerr)) {
        deferred_error = std::make_error_code(std::errc::io_error);
      }
      final_report_path = *resolved_path;
    }
  }

  if (options_.quiet) {
    for (size_t index = 0; index < jobs.size(); ++index) {
      const auto& report = common_context->reports[index];
      const bool success = report.has_value() && report->success;
      if (!success) {
        if (report.has_value()) {
          for (const auto& diag : report->diagnostics) {
            writer->Error(fmt::format("{}: {}", diag.code, diag.message));
          }
        } else {
          writer->Warning("No report available");
        }
      }
    }
  } else {
    const auto elapsed_ms
      = std::chrono::duration<double, std::milli>(common_context->state.elapsed)
          .count();
    writer->Info(
      fmt::format("Summary: jobs={} succeeded={} failed={} canceled={} "
                  "not_started={} total_time_ms={}",
        jobs.size(), counts.succeeded, counts.failed, counts.canceled,
        counts.not_started, elapsed_ms));

    for (size_t index = 0; index < jobs.size(); ++index) {
      const auto& report = common_context->reports[index];
      const auto status = JobStatusFromReport(report);
      writer->Info(fmt::format("Job {}: {}", DisplayJobNumber(index), status));
      if (report.has_value() && !report->success) {
        for (const auto& diag : report->diagnostics) {
          writer->Error(fmt::format("{}: {}", diag.code, diag.message));
        }
      }
    }
  }

  if (final_report_path.has_value()) {
    writer->Info(
      fmt::format("Report written: {}", final_report_path->string()));
  }

  if (deferred_error.has_value()) {
    return std::unexpected(*deferred_error);
  }

  if (common_context->exit_code != 0) {
    return std::unexpected(
      std::make_error_code(std::errc::state_not_recoverable));
  }
  return {};
}
} // namespace oxygen::content::import::tool
