//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Internal/ImportJob.h>
#include <Oxygen/OxCo/Algorithms.h>

namespace oxygen::content::import::detail {

namespace {

  [[nodiscard]] auto MakeZeroTelemetry() -> ImportTelemetry
  {
    return ImportTelemetry {
      .io_duration = std::chrono::microseconds { 0 },
      .source_load_duration = std::chrono::microseconds { 0 },
      .decode_duration = std::chrono::microseconds { 0 },
      .load_duration = std::chrono::microseconds { 0 },
      .cook_duration = std::chrono::microseconds { 0 },
      .emit_duration = std::chrono::microseconds { 0 },
      .finalize_duration = std::chrono::microseconds { 0 },
      .total_duration = std::chrono::microseconds { 0 },
    };
  }

  [[nodiscard]] auto VirtualMountRootLeaf(const ImportRequest& request)
    -> std::filesystem::path
  {
    auto mount_root
      = std::filesystem::path(request.loose_cooked_layout.virtual_mount_root)
          .lexically_normal();
    auto leaf = mount_root.filename();
    if (!leaf.empty()) {
      return leaf;
    }

    // Defensive fallback: virtual mount roots are expected to end with a
    // directory name (e.g. "/.cooked").
    return std::filesystem::path(".cooked");
  }

  [[nodiscard]] auto ResolveCookedRootForRequest(const ImportRequest& request)
    -> std::filesystem::path
  {
    const auto mount_leaf = VirtualMountRootLeaf(request);

    std::filesystem::path base_root;
    if (request.cooked_root.has_value()) {
      base_root = *request.cooked_root;
    } else if (!request.source_path.empty()) {
      std::error_code ec;
      auto absolute_source = std::filesystem::absolute(request.source_path, ec);
      if (!ec) {
        base_root = absolute_source.parent_path();
      }
    }

    if (base_root.empty()) {
      base_root = std::filesystem::temp_directory_path();
    }

    // Ensure the cooked root ends with the virtual mount root leaf directory
    // (e.g. ".cooked"). This keeps incremental imports and updates stable.
    if (base_root.filename() == mount_leaf) {
      return base_root;
    }

    return base_root / mount_leaf;
  }

} // namespace

ImportJob::ImportJob(ImportJobParams params)
  : job_id_(params.id)
  , request_(std::move(params.request))
  , on_complete_(std::move(params.on_complete))
  , on_progress_(std::move(params.on_progress))
  , cancel_event_(std::move(params.cancel_event))
  , file_reader_(params.reader)
  , file_writer_(params.writer)
  , thread_pool_(params.thread_pool)
  , table_registry_(params.registry)
  , index_registry_(params.index_registry)
  , concurrency_(params.concurrency)
  , stop_token_(std::move(params.stop_token))
{
  CHECK_NOTNULL_F(thread_pool_, "ImportJob requires a non-null thread pool");
}

auto ImportJob::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  return co::OpenNursery(nursery_, std::move(started));
}

void ImportJob::Run()
{
  DCHECK_F(
    nursery_ != nullptr, "ImportJob::Run() called before ActivateAsync()");
  DCHECK_F(!started_, "ImportJob::Run() called more than once");
  started_ = true;

  nursery_->Start([this]() -> co::Co<> { co_await MainAsync(); });
}

void ImportJob::Stop()
{
  stop_source_.request_stop();
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }
}

auto ImportJob::IsRunning() const -> bool { return nursery_ != nullptr; }

auto ImportJob::Wait() -> co::Co<> { co_await completed_; }

auto ImportJob::GetJobId() const noexcept -> ImportJobId { return job_id_; }

auto ImportJob::GetName() const noexcept -> std::string_view { return name_; }

void ImportJob::SetName(std::string_view name) noexcept
{
  name_.assign(name.begin(), name.end());
}

auto ImportJob::Request() -> ImportRequest& { return request_; }

auto ImportJob::Request() const -> const ImportRequest& { return request_; }

/*!
 Ensure the request has a concrete cooked root and create it on disk.

 Uses the request's explicit cooked root when provided. Otherwise, derives a
 cooked root from the source path and loose cooked layout. If the source path
 cannot be resolved, falls back to the process temp directory.
*/
auto ImportJob::EnsureCookedRoot() -> void
{
  auto cooked_root = ResolveCookedRootForRequest(request_);
  request_.cooked_root = cooked_root;

  std::error_code ec;
  std::filesystem::create_directories(cooked_root, ec);
  if (ec) {
    LOG_F(WARNING, "Failed to create cooked root '{}': {}",
      cooked_root.string(), ec.message());
  }
}

auto ImportJob::FileReader() const noexcept -> observer_ptr<IAsyncFileReader>
{
  return file_reader_;
}

auto ImportJob::FileWriter() const noexcept -> observer_ptr<IAsyncFileWriter>
{
  return file_writer_;
}

auto ImportJob::ThreadPool() const noexcept -> observer_ptr<co::ThreadPool>
{
  return thread_pool_;
}

auto ImportJob::Concurrency() const noexcept -> const ImportConcurrency&
{
  return concurrency_;
}

auto ImportJob::TableRegistry() const noexcept
  -> observer_ptr<ResourceTableRegistry>
{
  return table_registry_;
}

auto ImportJob::IndexRegistry() const noexcept
  -> observer_ptr<LooseCookedIndexRegistry>
{
  return index_registry_;
}

auto ImportJob::JobId() const -> ImportJobId { return job_id_; }

auto ImportJob::StopToken() const noexcept -> std::stop_token
{
  return stop_source_.get_token();
}

auto ImportJob::IsStopped() const noexcept -> bool
{
  return stop_source_.stop_requested() || stop_token_.stop_requested();
}

auto ImportJob::GetNamingService() -> NamingService&
{
  if (!naming_service_) {
    NamingService::Config config;
    if (request_.options.naming_strategy) {
      config.strategy = request_.options.naming_strategy;
    } else {
      config.strategy = std::make_shared<NoOpNamingStrategy>();
    }
    naming_service_ = std::make_unique<NamingService>(std::move(config));
  }
  return *naming_service_;
}

auto ImportJob::ProgressCallback() const noexcept
  -> const ProgressEventCallback&
{
  return on_progress_;
}

auto ImportJob::MainAsync() -> co::Co<>
{
  bool finalized = false;

  ReportJobEvent(
    ProgressEventKind::kJobStarted, ImportPhase::kPending, 0.0f, "Job started");

  auto make_exception_report = [&](std::string_view message) -> ImportReport {
    ImportReport report {
      .cooked_root
      = request_.cooked_root.value_or(request_.source_path.parent_path()),
      .success = false,
    };

    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = "import.exception",
      .message = std::string(message),
      .source_path = request_.source_path.string(),
    });

    return report;
  };

  auto finalize = [&](ImportReport report) {
    if (finalized) {
      return;
    }
    finalized = true;

    const auto phase
      = report.success ? ImportPhase::kComplete : ImportPhase::kFailed;
    ReportJobEvent(ProgressEventKind::kJobFinished, phase, 1.0f,
      report.success ? "Job finished" : "Job failed");

    DLOG_F(2, "Finalize: job_id={} success={}", job_id_, report.success);

    if (on_complete_) {
      on_complete_(job_id_, report);
    }

    completed_.Trigger();

    // Close the job nursery after reporting completion. This lets the parent
    // importer await job completion by joining the ActivateAsync task.
    Stop();
  };

  // Guarantee: call on_complete exactly once, even if this coroutine is
  // canceled by importer shutdown. Note that code after a cancellable
  // await is not guaranteed to run, so finalization must be done inside the
  // branches.
  co_await co::AnyOf(
    [&]() -> co::Co<> {
      auto run_work = [&]() -> co::Co<ImportReport> {
        if (cancel_event_ && cancel_event_->Triggered()) {
          stop_source_.request_stop();
          co_return MakeCancelledReport(request_);
        }

        if (cancel_event_) {
          auto [canceled, maybe_report]
            = co_await co::AnyOf(*cancel_event_, ExecuteAsync());
          if (canceled.has_value()) {
            stop_source_.request_stop();
            co_return MakeCancelledReport(request_);
          }

          DCHECK_F(maybe_report.has_value());
          co_return std::move(*maybe_report);
        }

        co_return co_await ExecuteAsync();
      };

      try {
        finalize(co_await run_work());
      } catch (const std::exception& ex) {
        const bool canceled = stop_source_.stop_requested()
          || (cancel_event_ && cancel_event_->Triggered());
        if (canceled) {
          finalize(MakeCancelledReport(request_));
        } else {
          LOG_F(ERROR, "Job failed: {}", ex.what());
          finalize(make_exception_report(ex.what()));
        }
      } catch (...) {
        const bool canceled = stop_source_.stop_requested()
          || (cancel_event_ && cancel_event_->Triggered());
        if (canceled) {
          finalize(MakeCancelledReport(request_));
        } else {
          LOG_F(ERROR, "Job failed: unknown exception");
          finalize(make_exception_report("unknown exception"));
        }
      }
      co_return;
    }(),
    co::UntilCancelledAnd([&]() -> co::Co<> {
      if (finalized) {
        co_return;
      }

      DLOG_F(2, "Job main canceled: job_id={}", job_id_);
      stop_source_.request_stop();
      finalize(MakeCancelledReport(request_));
      co_return;
    }));

  co_return;
}

auto ImportJob::MakeCancelledReport(const ImportRequest& request) const
  -> ImportReport
{
  ImportReport report {
    .cooked_root
    = request.cooked_root.value_or(request.source_path.parent_path()),
    .success = false,
  };

  report.telemetry = MakeZeroTelemetry();

  report.diagnostics.push_back({
    .severity = ImportSeverity::kInfo,
    .code = "import.canceled",
    .message = "Import canceled",
    .source_path = request.source_path.string(),
  });

  return report;
}

auto ImportJob::MakeNoFileWriterReport(const ImportRequest& request) const
  -> ImportReport
{
  ImportReport report {
    .cooked_root
    = request.cooked_root.value_or(request.source_path.parent_path()),
    .success = false,
  };

  report.telemetry = MakeZeroTelemetry();

  report.diagnostics.push_back({
    .severity = ImportSeverity::kError,
    .code = "import.no_file_writer",
    .message = "AsyncImporter has no IAsyncFileWriter configured",
    .source_path = request.source_path.string(),
  });

  return report;
}

auto ImportJob::ReportJobEvent(ProgressEventKind kind, ImportPhase phase,
  float overall_progress, std::string message) -> void
{
  if (!on_progress_) {
    return;
  }

  DCHECK_F(kind == ProgressEventKind::kJobStarted
      || kind == ProgressEventKind::kJobFinished,
    "ReportJobEvent expects job start or finish kind");
  ProgressEvent progress = kind == ProgressEventKind::kJobStarted
    ? MakeJobStarted(job_id_, phase, overall_progress, std::move(message))
    : MakeJobFinished(job_id_, phase, overall_progress, std::move(message));
  on_progress_(progress);
}

auto ImportJob::ReportPhaseProgress(
  ImportPhase phase, float overall_progress, std::string message) -> void
{
  if (!on_progress_) {
    return;
  }

  auto progress
    = MakePhaseProgress(job_id_, phase, overall_progress, std::move(message));
  on_progress_(progress);
}

auto ImportJob::ReportItemProgress(ProgressEventKind kind, ImportPhase phase,
  float overall_progress, std::string message, std::string item_kind,
  std::string item_name) -> void
{
  if (!on_progress_) {
    return;
  }

  DCHECK_F(kind == ProgressEventKind::kItemStarted
      || kind == ProgressEventKind::kItemFinished,
    "ReportItemProgress expects item start or finish kind");
  auto progress = kind == ProgressEventKind::kItemStarted
    ? MakeItemStarted(job_id_, phase, overall_progress, std::move(item_kind),
        std::move(item_name), std::move(message))
    : MakeItemFinished(job_id_, phase, overall_progress, std::move(item_kind),
        std::move(item_name), std::move(message));
  on_progress_(progress);
}

} // namespace oxygen::content::import::detail
