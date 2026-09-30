//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>
#include <Oxygen/Cooker/Import/Internal/ImportJobParams.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/ImportSourceSnapshot.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Import/RetainedModelImport.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import::detail {

namespace {

  [[nodiscard]] auto HasGenerationMarker(std::filesystem::path path) -> bool
  {
    path = base::ToLogicalPath(
      std::filesystem::weakly_canonical(base::ToNativePath(path)));
    while (!path.empty()) {
      if (std::filesystem::exists(base::ToNativePath(
            path / data::loose_cooked::kGenerationLeaseFileName))) {
        return true;
      }
      const auto parent = path.parent_path();
      if (parent == path) {
        break;
      }
      path = parent;
    }
    return false;
  }

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
  , script_compile_callback_(std::move(params.script_compile_callback))
  , stop_token_(std::move(params.stop_token))
  , retained_import_(std::move(params.retained_import))
{
  CHECK_NOTNULL_F(thread_pool_, "ImportJob requires a non-null thread pool");
  if (retained_import_) {
    CHECK_NOTNULL_F(file_reader_, "Retained imports require a source reader");
    source_snapshot_
      = std::make_shared<ImportSourceSnapshot>(*file_reader_, *thread_pool_);
    file_reader_ = observer_ptr { source_snapshot_.get() };
    CHECK_NOTNULL_F(
      file_writer_, "Retained imports require a generation writer");
    generation_writer_ = std::move(params.generation_writer);
    CHECK_NOTNULL_F(
      generation_writer_, "Retained imports require an isolated native writer");
    file_writer_ = observer_ptr { generation_writer_.get() };
    generation_tables_ = std::make_unique<ResourceTableRegistry>(*file_writer_);
    generation_index_ = std::make_unique<LooseCookedIndexRegistry>();
    table_registry_ = observer_ptr { generation_tables_.get() };
    index_registry_ = observer_ptr { generation_index_.get() };
  }
}

ImportJob::~ImportJob() = default;

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

  nursery_->Start([this] -> co::Co<> { co_await MainAsync(); });
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
auto ImportJob::WritableCookedRoot() const -> std::filesystem::path
{
  const auto root = request_.ResolveCookedRoot();
  if (HasGenerationMarker(root)
    && (!retained_import_ || !retained_import_->AllowsWriting(root))) {
    throw std::invalid_argument(
      "Immutable import generations require their prepared write authority");
  }
  return root;
}

auto ImportJob::EnsureCookedRoot() -> void
{
  const auto cooked_root = WritableCookedRoot();
  request_.cooked_root = cooked_root;
  std::error_code ec;
  std::filesystem::create_directories(base::ToNativePath(cooked_root), ec);
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

auto ImportJob::ScriptCompileCallback() const noexcept
  -> const AsyncImportService::ScriptCompileCallback&
{
  return script_compile_callback_;
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
      NormalizeNamingStrategy::Options normalize_options {};
      // Keep authored naming style, but normalize unsafe characters so emitted
      // descriptor virtual paths remain canonical.
      normalize_options.apply_prefixes = false;
      config.strategy
        = std::make_shared<NormalizeNamingStrategy>(normalize_options);
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

auto ImportJob::Session() -> ImportSession&
{
  if (!session_) {
    session_ = std::make_unique<ImportSession>(request_, file_reader_,
      file_writer_, thread_pool_, table_registry_, index_registry_);
  }
  return *session_;
}

auto ImportJob::RequestProducerStop() -> void
{
  stop_source_.request_stop();
  if (producer_nursery_) {
    producer_nursery_->Cancel();
  }
}

auto ImportJob::ExecuteAndPublishAsync() -> co::Co<ImportReport>
{
  const auto cooked_root = WritableCookedRoot();
  if (table_registry_) {
    co_await table_registry_->WaitForFinalization(cooked_root);
  }
  if (retained_import_) {
    co_await thread_pool_->Run(
      [publication = retained_import_](
        co::ThreadPool::CancelToken cancelled) -> void {
        if (!cancelled) {
          try {
            static_cast<void>(publication->ReclaimUnusedGenerations());
          } catch (const std::exception& error) {
            LOG_F(WARNING, "Retained generation cleanup deferred: {}",
              error.what());
          }
        }
      });
  }
  auto report = co_await ExecuteAsync();
  if (!retained_import_ || !report.success) {
    co_return report;
  }
  auto candidate = std::make_shared<ImportReport>(std::move(report));
  const bool validated = co_await thread_pool_->Run(
    [publication = retained_import_, candidate](
      co::ThreadPool::CancelToken cancelled) -> bool {
      if (cancelled) {
        return false;
      }
      publication->ValidateCandidate(*candidate);
      return true;
    });
  if (!validated) {
    co_return MakeCancelledReport(request_);
  }
  co_await source_snapshot_->Verify();
  if (IsStopped() || (cancel_event_ && cancel_event_->Triggered())) {
    co_return MakeCancelledReport(request_);
  }
  // The short CAS/atomic-replace section does not suspend. Once it commits,
  // completion wins over a cancellation delivered afterward.
  retained_import_->Publish(*candidate, StopToken());
  co_return std::move(*candidate);
}

auto ImportJob::MainAsync() -> co::Co<>
{
  // Cancellation stops producers. The owning orchestrator stays alive to join
  // them and then drain callbacks before destroying session-owned emitters.
  co_await co::AnyOf(co::NonCancellable(RunOwnedSessionToCompletion()),
    co::UntilCancelledAnd([this]() -> co::Co<> {
      RequestProducerStop();
      co_return;
    }));
}

auto ImportJob::RunOwnedSessionToCompletion() -> co::Co<>
{
  const auto completion = ScopeGuard([this]() noexcept {
    completed_.Trigger();
    Stop();
  });
  ImportReport report {};
  bool execution_completed = false;
  std::exception_ptr execution_failure;
  std::exception_ptr drain_failure;
  const auto fail = [&](const std::string_view message) {
    report.success = false;
    report.cooked_root
      = request_.cooked_root.value_or(request_.source_path.parent_path());
    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = "import.exception",
      .message = std::string(message),
      .source_path = request_.source_path.string(),
      .object_path = {},
    });
  };
  try {
    ReportJobEvent(ProgressEventKind::kJobStarted, ImportPhase::kPending, 0.0F,
      "Job started");
    OXCO_WITH_NURSERY(producers)
    {
      producer_nursery_ = &producers;
      if (IsStopped() || (cancel_event_ && cancel_event_->Triggered())) {
        co_return co::kCancel;
      }
      if (cancel_event_) {
        producers.Start([this]() -> co::Co<> {
          co_await *cancel_event_;
          RequestProducerStop();
        });
      }
      report = co_await ExecuteAndPublishAsync();
      execution_completed = true;
      co_return co::kCancel;
    };
  } catch (...) {
    execution_failure = std::current_exception();
  }
  producer_nursery_ = nullptr;
  pipelines_.clear();
  try {
    if (session_) {
      co_await session_->DrainAndRetire();
      session_.reset();
    }
  } catch (...) {
    drain_failure = std::current_exception();
  }

  // Diagnostics may allocate. Teardown must finish even when the original
  // failure was allocation failure in table/index finalization.
  const auto describe_failure
    = [&](const std::exception_ptr& failure) noexcept {
        report.success = false;
        try {
          try {
            std::rethrow_exception(failure);
          } catch (const std::exception& error) {
            fail(error.what());
          } catch (...) {
            fail("Unknown import exception");
          }
        } catch (...) {
          // A minimal failed report still carries completion during memory
          // exhaustion.
        }
      };
  if (execution_failure && !IsStopped()) {
    describe_failure(execution_failure);
  } else if (!execution_completed) {
    try {
      report = MakeCancelledReport(request_);
    } catch (...) {
      report.success = false;
    }
  }
  if (drain_failure) {
    describe_failure(drain_failure);
  }
  try {
    ReportJobEvent(ProgressEventKind::kJobFinished,
      report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
      report.success ? "Job finished" : "Job failed");
  } catch (...) {
    describe_failure(std::current_exception());
  }
  if (on_complete_) {
    try {
      on_complete_(job_id_, report);
    } catch (const std::exception& error) {
      LOG_F(ERROR, "Import completion callback failed: {}", error.what());
    } catch (...) {
      LOG_F(ERROR, "Import completion callback threw an unknown exception");
    }
  }
}

auto ImportJob::MakeCancelledReport(const ImportRequest& request) const
  -> ImportReport
{
  auto report = ImportReport {};
  report.cooked_root
    = request.cooked_root.value_or(request.source_path.parent_path());
  report.success = false;

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
  auto report = ImportReport {};
  report.cooked_root
    = request.cooked_root.value_or(request.source_path.parent_path());
  report.success = false;

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
