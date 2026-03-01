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
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ReportJson.h>
#include <Oxygen/Cooker/Tools/ImportTool/UI/BatchViewModel.h>
#include <Oxygen/Cooker/Tools/ImportTool/UI/Screens/BatchImportScreen.h>

#ifndef OXYGEN_IMPORT_TOOL_VERSION
#  error OXYGEN_IMPORT_TOOL_VERSION must be defined for ImportTool reports.
#endif

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::ordered_json;
  using oxygen::ScopeGuard;
  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;

  // Helper structs for logic (kept for now, or moved to ViewModel later)
  struct PreparedJob {
    ImportRequest request;
    std::string job_type;
    std::string source_path;
    std::string job_id;
    std::vector<std::string> depends_on;
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
      entry.input_queue_load = 0.0f;
      entry.output_queue_load = 0.0f;
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

  auto ResolveReportJobType(const ImportRequest& request) -> std::string
  {
    if (request.options.input.has_value()) {
      return "input";
    }
    if (request.GetFormat() != ImportFormat::kUnknown) {
      return std::string(to_string(request.GetFormat()));
    }
    switch (request.options.scripting.import_kind) {
    case ScriptingImportKind::kScriptAsset:
      return "script";
    case ScriptingImportKind::kScriptingSidecar:
      return "script-sidecar";
    case ScriptingImportKind::kNone:
      break;
    }
    return "unknown";
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

  struct BatchSummaryCounts {
    size_t succeeded = 0;
    size_t failed = 0;
    size_t skipped = 0;
  };

  auto BuildBatchSummary(
    const std::vector<std::optional<ImportReport>>& reports,
    const size_t total_jobs) -> BatchSummaryCounts
  {
    BatchSummaryCounts counts {};
    for (const auto& report : reports) {
      if (!report.has_value()) {
        ++counts.skipped;
        continue;
      }
      const auto status = JobStatusFromReport(*report);
      if (status == "succeeded") {
        ++counts.succeeded;
      } else if (status == "skipped") {
        ++counts.skipped;
      } else {
        ++counts.failed;
      }
    }
    if (counts.succeeded + counts.skipped <= total_jobs) {
      counts.failed = total_jobs - counts.succeeded - counts.skipped;
    }
    return counts;
  }

  auto MakeZeroTelemetry() -> ImportTelemetry
  {
    constexpr auto kZero = std::chrono::microseconds { 0 };
    return ImportTelemetry {
      .io_duration = kZero,
      .source_load_duration = kZero,
      .decode_duration = kZero,
      .load_duration = kZero,
      .cook_duration = kZero,
      .emit_duration = kZero,
      .finalize_duration = kZero,
      .total_duration = kZero,
    };
  }

  auto MakeSkippedDependencyReport(const ImportRequest& request,
    const std::string_view failed_job_id) -> ImportReport
  {
    auto report = ImportReport {};
    report.success = false;
    report.telemetry = MakeZeroTelemetry();
    report.diagnostics.push_back({
      .severity = ImportSeverity::kWarning,
      .code = "input.import.skipped_predecessor_failed",
      .message = failed_job_id.empty()
        ? "Skipped because a predecessor job failed"
        : fmt::format("Skipped because predecessor '{}' failed", failed_job_id),
      .source_path = request.source_path.string(),
      .object_path = {},
    });
    report.diagnostics.push_back({
      .severity = ImportSeverity::kInfo,
      .code = "import.canceled",
      .message = "Job was skipped due to failed dependency",
      .source_path = request.source_path.string(),
      .object_path = {},
    });
    return report;
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
    .WithOption(std::move(report))
    .WithOption(std::move(max_in_flight));
}

auto BatchCommand::PrepareImportServiceConfig()
  -> std::expected<AsyncImportService::Config, std::error_code>
{
  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto writer = global_options_->writer;

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
  const auto session_started = std::chrono::system_clock::now();
  // 1. Process Options
  const bool fail_fast
    = global_options_ != nullptr && global_options_->fail_fast;
  const bool quiet = global_options_ != nullptr && global_options_->quiet;
  // TUI control is global-only; respect the global --no-tui setting.

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
      && job.job_type != "gltf" && job.job_type != "script"
      && job.job_type != "script-sidecar" && job.job_type != "input") {
      writer->Error(
        fmt::format("ERROR: unsupported job type: {}", job.job_type));
      unsupported_seen = true;
      ++validation_failures;
      if (fail_fast) {
        return std::unexpected(std::make_error_code(std::errc::not_supported));
      }
      continue;
    }

    std::optional<ImportRequest> request;
    {
      std::ostringstream err;
      request = job.BuildRequest(err);
      if (!request.has_value()) {
        const auto msg = err.str();
        if (!msg.empty()) {
          writer->Error(msg);
        }
        ++validation_failures;
        if (fail_fast) {
          break;
        }
        continue;
      }
    }

    if (global_options_ != nullptr && !global_options_->cooked_root.empty()
      && request->cooked_root.has_value() && request->cooked_root->empty()) {
      request->cooked_root
        = std::filesystem::path(global_options_->cooked_root);
    } else if (global_options_ != nullptr && !request->cooked_root.has_value()
      && !global_options_->cooked_root.empty()) {
      request->cooked_root
        = std::filesystem::path(global_options_->cooked_root);
    }
    if (options_.dry_run) {
      writer->Info(fmt::format(
        "DRY-RUN: {} {}", job.job_type, request->source_path.string()));
      continue;
    }

    auto prepared = PreparedJob {
      .request = *request,
      .job_type = job.job_type,
      .source_path = request->source_path.string(),
      .job_id = job.id,
      .depends_on = job.depends_on,
    };
    if (prepared.job_id.empty() && request->orchestration.has_value()) {
      prepared.job_id = request->orchestration->job_id;
      prepared.depends_on = request->orchestration->depends_on;
    }
    jobs.push_back(std::move(prepared));
  }

  std::unordered_map<std::string, size_t> job_id_to_index {};
  job_id_to_index.reserve(jobs.size());
  for (size_t index = 0; index < jobs.size(); ++index) {
    const auto& job_id = jobs[index].job_id;
    if (job_id.empty()) {
      continue;
    }
    if (!job_id_to_index.emplace(job_id, index).second) {
      writer->Error(fmt::format(
        "ERROR [input.manifest.job_id_duplicate]: duplicate job id '{}'",
        job_id));
      ++validation_failures;
    }
  }

  std::vector<std::vector<size_t>> dependents(jobs.size());
  std::vector<size_t> dependency_remaining(jobs.size(), 0U);
  for (size_t index = 0; index < jobs.size(); ++index) {
    auto unique_dep_ids = std::unordered_set<std::string> {};
    for (const auto& dep_id : jobs[index].depends_on) {
      if (!unique_dep_ids.insert(dep_id).second) {
        continue;
      }
      const auto it = job_id_to_index.find(dep_id);
      if (it == job_id_to_index.end()) {
        writer->Error(fmt::format(
          "ERROR [input.manifest.dep_missing_target]: job '{}' depends on "
          "missing id '{}'",
          jobs[index].job_id.empty()
            ? fmt::format("#{}", DisplayJobNumber(index))
            : jobs[index].job_id,
          dep_id));
        ++validation_failures;
        continue;
      }
      dependents[it->second].push_back(index);
      ++dependency_remaining[index];
    }
  }

  if (validation_failures == 0 && !jobs.empty()) {
    auto remaining = dependency_remaining;
    std::deque<size_t> ready;
    for (size_t index = 0; index < remaining.size(); ++index) {
      if (remaining[index] == 0U) {
        ready.push_back(index);
      }
    }
    size_t visited = 0U;
    while (!ready.empty()) {
      const auto node = ready.front();
      ready.pop_front();
      ++visited;
      for (const auto child : dependents[node]) {
        if (remaining[child] > 0U) {
          --remaining[child];
          if (remaining[child] == 0U) {
            ready.push_back(child);
          }
        }
      }
    }
    if (visited != jobs.size()) {
      writer->Error(
        "ERROR [input.manifest.dep_cycle]: dependency cycle detected in batch "
        "jobs");
      ++validation_failures;
    }
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

  if (validation_failures > 0 && fail_fast) {
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
                                      &submit_times, &dependents,
                                      &dependency_remaining,
                                      worker_totals](std::stop_token st) {
    DCHECK_NOTNULL_F(import_service, "Import service must be set by main");

    size_t submitted = 0;
    size_t completed = 0;
    size_t failures = 0;
    size_t in_flight = 0;
    std::vector<std::optional<ImportJobId>> job_ids(jobs.size());
    std::vector<ActiveJobView> job_views(jobs.size());
    std::vector<bool> job_active(jobs.size(), false);
    std::vector<bool> job_submitted(jobs.size(), false);
    std::vector<bool> job_finished(jobs.size(), false);
    std::vector<bool> predecessor_failed(jobs.size(), false);
    auto remaining_dependencies = dependency_remaining;
    auto ready_queue = std::deque<size_t> {};
    for (size_t index = 0; index < remaining_dependencies.size(); ++index) {
      if (remaining_dependencies[index] == 0U) {
        ready_queue.push_back(index);
      }
    }

    std::array<uint32_t, 7> outstanding_items { 0U, 0U, 0U, 0U, 0U, 0U, 0U };
    std::array<float, 7> input_queue_loads {
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
    };
    std::array<float, 7> output_queue_loads {
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
          entry.input_queue_load = input_queue_loads[*index];
          entry.output_queue_load = output_queue_loads[*index];
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

    while (!st.stop_requested() && (completed < jobs.size() || in_flight > 0)) {
      if (!import_service->IsAcceptingJobs()) {
        RequestShutdown();
      }

      if (shutdown_requested) {
        break;
      }

      while (!shutdown_requested) {
        if (st.stop_requested()) {
          RequestShutdown();
          break;
        }
        std::optional<size_t> maybe_job_index;
        {
          std::scoped_lock lock(common_context->mutex);
          while (!ready_queue.empty()) {
            const auto candidate = ready_queue.front();
            ready_queue.pop_front();
            if (!job_submitted[candidate] && !job_finished[candidate]) {
              maybe_job_index = candidate;
              break;
            }
          }
        }
        if (!maybe_job_index.has_value()) {
          break;
        }
        const auto job_index = *maybe_job_index;
        auto& job = jobs[job_index];

        auto on_complete = [&, job_index](
                             ImportJobId id, const ImportReport& report) {
          std::scoped_lock lock(common_context->mutex);

          if (job_ids[job_index].has_value() && id != *job_ids[job_index]) {
            const auto u_expected = job_ids[job_index]->get();
            const auto u_actual = id.get();
            common_context->state.recent_logs.push_back(
              fmt::format("Job {} id mismatch (expected {}, got {})",
                DisplayJobNumber(job_index), u_expected, u_actual));
          }

          job_finished[job_index] = true;

          job_views[job_index].progress = 1.0f;
          job_views[job_index].status = report.success ? "Completed" : "Failed";
          job_views[job_index].item_event = "";
          items_started[job_index].clear();
          items_finished[job_index].clear();
          job_views[job_index].items_completed = 0U;
          job_views[job_index].items_total = 0U;
          job_active[job_index] = false;
          auto& job_outstanding = per_job_outstanding[job_index];
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

          common_context->reports[job_index] = report;
          if (!report.success) {
            failures++;
            common_context->state.failures = failures;
            common_context->exit_code = 2; // Fail code

            for (const auto& diag : report.diagnostics) {
              common_context->state.recent_logs.push_back(
                fmt::format("✖ Job {} Failed: {}: {}",
                  DisplayJobNumber(job_index), diag.code, diag.message));
            }
          } else {
            common_context->state.recent_logs.push_back(
              fmt::format("✔ Job {} Completed", DisplayJobNumber(job_index)));
            writer->Report(
              fmt::format("Job {} Completed", DisplayJobNumber(job_index)));
          }

          // Cap logs
          if (common_context->state.recent_logs.size() > 50) {
            common_context->state.recent_logs.erase(
              common_context->state.recent_logs.begin(),
              common_context->state.recent_logs.end() - 50);
          }

          completed++;
          in_flight--;

          auto skip_queue = std::deque<std::pair<size_t, std::string>> {};
          const auto mark_dependency
            = [&](const size_t parent_index, const bool parent_success,
                const std::string& failed_job_id) {
                for (const auto child_index : dependents[parent_index]) {
                  if (job_finished[child_index]) {
                    continue;
                  }
                  if (!parent_success) {
                    predecessor_failed[child_index] = true;
                  }
                  if (remaining_dependencies[child_index] > 0U) {
                    --remaining_dependencies[child_index];
                  }
                  if (remaining_dependencies[child_index] == 0U) {
                    if (predecessor_failed[child_index]) {
                      skip_queue.emplace_back(child_index, failed_job_id);
                    } else {
                      ready_queue.push_back(child_index);
                    }
                  }
                }
              };

          const auto failed_id = jobs[job_index].job_id.empty()
            ? fmt::format("#{}", DisplayJobNumber(job_index))
            : jobs[job_index].job_id;
          mark_dependency(job_index, report.success, failed_id);

          while (!skip_queue.empty()) {
            auto [skip_index, failed_dep] = std::move(skip_queue.front());
            skip_queue.pop_front();
            if (job_finished[skip_index] || job_submitted[skip_index]) {
              continue;
            }

            job_finished[skip_index] = true;
            auto skipped = MakeSkippedDependencyReport(
              jobs[skip_index].request, failed_dep);
            common_context->reports[skip_index] = skipped;
            failures++;
            completed++;
            common_context->state.failures = failures;
            common_context->exit_code = 2;
            job_views[skip_index].progress = 1.0f;
            job_views[skip_index].status = "Skipped";
            job_views[skip_index].item_event = "";
            job_views[skip_index].items_completed = 0U;
            job_views[skip_index].items_total = 0U;
            job_active[skip_index] = false;
            common_context->state.recent_logs.push_back(
              fmt::format("↷ Job {} Skipped: predecessor failed ({})",
                DisplayJobNumber(skip_index), failed_dep));

            const auto next_failed = jobs[skip_index].job_id.empty()
              ? failed_dep
              : jobs[skip_index].job_id;
            mark_dependency(skip_index, false, next_failed);
          }

          if (common_context->state.recent_logs.size() > 50) {
            common_context->state.recent_logs.erase(
              common_context->state.recent_logs.begin(),
              common_context->state.recent_logs.end() - 50);
          }

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

        auto on_progress = [&, job_index](const ProgressEvent& progress) {
          const auto now = std::chrono::steady_clock::now();
          std::scoped_lock lock(common_context->mutex);
          UpdateProgressTrace(progress_traces[job_index], progress, now);
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
              = fmt::format("Job {}-{} {}", DisplayJobNumber(job_index),
                PhaseCode(progress.header.phase), event_label);
            if (const auto* item = GetItemProgress(progress)) {
              if (!item->item_name.empty()) {
                line.append(" ");
                line.append(item->item_name);
              }
              if (progress.header.kind == ProgressEventKind::kItemCollected) {
                line.append(fmt::format(" load={:.2f}|{:.2f}",
                  item->input_queue_load, item->output_queue_load));
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
            submit_times[job_index] = std::chrono::steady_clock::now();
            job_views[job_index].status = "Running";
          }

          job_views[job_index].progress = progress.header.overall_progress;
          job_views[job_index].status
            = std::string(nostd::to_string(progress.header.phase));

          if (const auto* item = GetItemProgress(progress)) {
            if (progress.header.kind == ProgressEventKind::kItemCollected) {
              if (!item->item_kind.empty()) {
                const auto index = WorkerKindIndex(item->item_kind);
                if (index.has_value()) {
                  DCHECK_F(item->input_queue_load >= 0.0f
                      && item->input_queue_load <= 1.0f,
                    "Item collection input queue load is out of range: {}",
                    item->input_queue_load);
                  DCHECK_F(item->output_queue_load >= 0.0f
                      && item->output_queue_load <= 1.0f,
                    "Item collection output queue load is out of range: {}",
                    item->output_queue_load);
                  input_queue_loads[*index] = item->input_queue_load;
                  output_queue_loads[*index] = item->output_queue_load;
                }
              }
            } else {
              if (!item->item_kind.empty()) {
                job_views[job_index].item_kind = item->item_kind;
              }
              if (!item->item_name.empty()) {
                job_views[job_index].item_name = item->item_name;
              }
              if (progress.header.kind == ProgressEventKind::kItemStarted) {
                job_views[job_index].item_event = "started";
              } else if (progress.header.kind
                == ProgressEventKind::kItemFinished) {
                job_views[job_index].item_event = "finished";
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
                    items_started[job_index].insert(key);
                  } else if (progress.header.kind
                    == ProgressEventKind::kItemFinished) {
                    items_finished[job_index].insert(key);
                  }
                  job_views[job_index].items_total
                    = static_cast<uint32_t>(items_started[job_index].size());
                  job_views[job_index].items_completed
                    = static_cast<uint32_t>(items_finished[job_index].size());
                }
              }

              if (!item->item_kind.empty()) {
                const auto index = WorkerKindIndex(item->item_kind);
                if (index.has_value()) {
                  auto& active = outstanding_items[*index];
                  auto& per_job = per_job_outstanding[job_index][*index];
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
          job_submitted[job_index] = true;
          job_ids[job_index] = *id;
          job_views[job_index].id = std::to_string(DisplayJobNumber(job_index));
          job_views[job_index].source = job.source_path;
          job_views[job_index].status = "Queued";
          job_views[job_index].progress = 0.0f;
          job_views[job_index].items_completed = 0U;
          job_views[job_index].items_total = 0U;
          job_active[job_index] = true;
          submitted++;
          in_flight++;
        } else {
          std::scoped_lock lock(common_context->mutex);
          ready_queue.push_front(job_index);
          common_context->state.recent_logs.push_back(
            fmt::format("Backpressure: delaying submission of job {}",
              DisplayJobNumber(job_index)));
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

      auto stalled = false;
      {
        std::scoped_lock lock(common_context->mutex);
        if (!shutdown_requested && in_flight == 0U && ready_queue.empty()
          && completed < jobs.size()) {
          common_context->state.recent_logs.push_back(
            "Dependency scheduler stalled with unfinished jobs");
          common_context->exit_code = 2;
          stalled = true;
        }
      }
      if (stalled) {
        RequestShutdown();
        break;
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
      const auto session_ended = std::chrono::system_clock::now();
      const auto elapsed_ms = std::chrono::duration<double, std::milli>(
        common_context->state.elapsed)
                                .count();
      CHECK_F(cooked_root.has_value() && !cooked_root->empty(),
        "Cooked root is required in report output");
      CHECK_F(!global_options_->command_line.empty(),
        "Command line is required in report output");

      double total_io_ms = 0.0;
      double total_cpu_ms = 0.0;
      ordered_json jobs_json = ordered_json::array();

      for (size_t index = 0; index < jobs.size(); ++index) {
        const auto& job = jobs[index];
        const auto& report = common_context->reports[index];

        const auto job_type = ResolveReportJobType(job.request);

        ordered_json job_json = ordered_json::object();
        job_json["index"] = DisplayJobNumber(index);
        job_json["type"] = job_type;
        job_json["work_items"] = BuildWorkItemsJson(
          progress_traces[index], job_type, job.request.source_path.string());
        if (report.has_value()) {
          const auto& report_value = *report;
          total_io_ms += ComputeIoMillis(report_value.telemetry);
          total_cpu_ms += ComputeCpuMillis(report_value.telemetry);
          job_json["status"] = std::string(JobStatusFromReport(report_value));
          job_json["outputs"] = BuildOutputsJson(report_value.outputs);
          job_json["stats"] = BuildStatsJson(report_value.telemetry);
          job_json["diagnostics"]
            = BuildDiagnosticsJson(report_value.diagnostics);
        } else {
          job_json["status"] = "not_submitted";
          job_json["outputs"] = ordered_json::array();
          job_json["stats"] = BuildEmptyStatsJson();
          job_json["diagnostics"] = ordered_json::array();
        }
        jobs_json.push_back(std::move(job_json));
      }

      ordered_json payload = ordered_json::object();
      payload["report_version"] = std::string(kReportVersion);
      payload["session"] = {
        { "id", MakeSessionId(session_started) },
        { "started_utc", FormatUtcTimestamp(session_started) },
        { "ended_utc", FormatUtcTimestamp(session_ended) },
        { "tool_version", std::string(OXYGEN_IMPORT_TOOL_VERSION) },
        { "command_line", std::string(global_options_->command_line) },
        { "cooked_root", cooked_root->string() },
      };
      payload["summary"] = {
        { "jobs_total", jobs.size() },
        { "jobs_succeeded", counts.succeeded },
        { "jobs_failed", counts.failed },
        { "jobs_skipped", counts.skipped },
        { "time_ms_total", elapsed_ms },
        { "time_ms_io", total_io_ms },
        { "time_ms_cpu", total_cpu_ms },
      };
      payload["jobs"] = std::move(jobs_json);

      if (!WriteJsonReport(payload, *resolved_path, std::cerr)) {
        deferred_error = std::make_error_code(std::errc::io_error);
      }
      final_report_path = *resolved_path;
    }
  }

  if (quiet) {
    for (size_t index = 0; index < jobs.size(); ++index) {
      const auto& report = common_context->reports[index];
      if (!report.has_value()) {
        continue;
      }
      if (!report->success) {
        for (const auto& diag : report->diagnostics) {
          const auto message = fmt::format("{}: {}", diag.code, diag.message);
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
      }
    }
  } else {
    const auto elapsed_ms
      = std::chrono::duration<double, std::milli>(common_context->state.elapsed)
          .count();
    writer->Info(
      fmt::format("Summary: jobs={} succeeded={} failed={} skipped={} "
                  "total_time_ms={}",
        jobs.size(), counts.succeeded, counts.failed, counts.skipped,
        elapsed_ms));

    for (size_t index = 0; index < jobs.size(); ++index) {
      const auto& report = common_context->reports[index];
      if (!report.has_value()) {
        writer->Info(
          fmt::format("Job {}: not_submitted", DisplayJobNumber(index)));
        continue;
      }
      const auto status = JobStatusFromReport(*report);
      writer->Info(fmt::format("Job {}: {}", DisplayJobNumber(index), status));
      if (!report->success) {
        for (const auto& diag : report->diagnostics) {
          const auto message = fmt::format("{}: {}", diag.code, diag.message);
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
