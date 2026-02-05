//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/AsyncImportService.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/IAsyncFileReader.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/ImportConcurrency.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportJobId.h>
#include <Oxygen/Content/Import/ImportManifest.h>
#include <Oxygen/Content/Import/ImportProgress.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Internal/AsyncImporter.h>
#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/ImportJob.h>
#include <Oxygen/Content/Import/Internal/ImportJobParams.h>
#include <Oxygen/Content/Import/Internal/JobEntry.h>
#include <Oxygen/Content/Import/Internal/Jobs/FbxImportJob.h>
#include <Oxygen/Content/Import/Internal/Jobs/GlbImportJob.h>
#include <Oxygen/Content/Import/Internal/Jobs/TextureImportJob.h>
#include <Oxygen/Content/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto MakeJobName(ImportFormat format, ImportJobId job_id,
    const std::filesystem::path& source_path) -> std::string
  {
    const auto file_name = source_path.filename().string();
    const auto name_part = file_name.empty() ? "source" : file_name;
    return std::string(nostd::to_string(format)) + ":"
      + nostd::to_string(job_id) + ":" + name_part;
  }

  [[nodiscard]] auto CreateJobForFormat(ImportFormat format,
    detail::ImportJobParams params) -> std::shared_ptr<detail::ImportJob>
  {
    switch (format) {
    case ImportFormat::kFbx:
      return std::make_shared<detail::FbxImportJob>(std::move(params));
    case ImportFormat::kGltf:
      return std::make_shared<detail::GlbImportJob>(std::move(params));
    case ImportFormat::kTextureImage:
      return std::make_shared<detail::TextureImportJob>(std::move(params));
    case ImportFormat::kUnknown:
      break;
    }
    return {};
  }

} // namespace

//=== Implementation
//===-------------------------------------------------------//

struct AsyncImportService::Impl {
  static constexpr size_t kImportChannelCapacity = 64;
  explicit Impl(const Config& config)
    : config_(config)
  {
  }

  ~Impl() = default;

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  //! Service configuration.
  Config config_;

  //! The import thread.
  std::thread import_thread_;

  //! Event loop running on the import thread.
  std::unique_ptr<ImportEventLoop> event_loop_;

  //! Async file reader (created on import thread).
  std::unique_ptr<IAsyncFileReader> file_reader_;

  //! Async file writer (created on import thread).
  std::unique_ptr<IAsyncFileWriter> file_writer_;

  //! Resource table registry (created on import thread).
  std::unique_ptr<ResourceTableRegistry> table_registry_;

  //! Loose cooked index registry (created on import thread).
  std::unique_ptr<LooseCookedIndexRegistry> index_registry_;

  //! Thread pool for CPU-bound import work (created on import thread).
  std::unique_ptr<co::ThreadPool> thread_pool_;

  //! Next job ID to assign.
  std::atomic<uint64_t> next_job_id_ { 1 };

  //! Lightweight cancellation tracking only.
  mutable std::mutex cancel_events_mutex_;

  //! Per-job cancellation events.
  std::unordered_map<ImportJobId, std::shared_ptr<co::Event>> cancel_events_;

  //! The async importer LiveObject (created on import thread).
  std::unique_ptr<detail::AsyncImporter> async_importer_;

  //! Flag indicating shutdown has been requested (for rejecting new jobs).
  std::atomic<bool> shutdown_requested_ { false };

  //! Flag indicating full shutdown has completed.
  std::atomic<bool> shutdown_complete_ { false };

  //! Primary stop source for all jobs.
  std::stop_source stop_source_;

  //! Serialize shutdown operations.
  std::mutex shutdown_mutex_;

  //! Flag indicating the import thread is running and ready.
  std::atomic<bool> thread_running_ { false };

  //! Latch to signal thread startup complete.
  std::latch startup_latch_ { 1 };

  //! Start the import thread and wait for it to be ready.
  auto StartThread() -> void
  {
    import_thread_ = std::thread([this]() -> void { ThreadMain(); });

    // Wait for the import thread to finish initialization
    startup_latch_.wait();
  }

  //! Main function running on the import thread.
  auto ThreadMain() -> void
  {
    DLOG_F(INFO, "Import thread started");

    // Create event loop on this thread
    event_loop_ = std::make_unique<ImportEventLoop>();

    // Create platform-specific file reader via factory
    file_reader_ = CreateAsyncFileReader(*event_loop_);

    // Create platform-specific file writer via factory
    file_writer_ = CreateAsyncFileWriter(*event_loop_);

    table_registry_ = std::make_unique<ResourceTableRegistry>(*file_writer_);
    index_registry_ = std::make_unique<LooseCookedIndexRegistry>();

    // Create thread pool for CPU-bound work (pipelines, mesh processing)
    thread_pool_ = std::make_unique<co::ThreadPool>(
      *event_loop_, config_.thread_pool_size);

    // Create the async importer
    async_importer_
      = std::make_unique<detail::AsyncImporter>(detail::AsyncImporter::Config {
        .channel_capacity = kImportChannelCapacity,
        .max_in_flight_jobs = config_.max_in_flight_jobs,
        .file_writer = file_writer_.get(),
        .table_registry = table_registry_.get(),
      });

    thread_running_.store(true, std::memory_order_release);

    // Signal that initialization is complete
    startup_latch_.count_down();

    // Run the coroutine runtime with the AsyncImporter
    // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines) - blocking call
    co::Run(*event_loop_, [this]() -> co::Co<> {
      // Use OXCO_WITH_NURSERY to properly activate the LiveObject
      OXCO_WITH_NURSERY(n)
      {
        // Start the activation task with suspending Start()
        // This ensures the nursery is open before Run() is called
        // Note: n.Start() will provide the TaskStarted argument internally
        co_await n.Start(
          &detail::AsyncImporter::ActivateAsync, async_importer_.get());

        // Start the job processing loop
        async_importer_->Run();

        // Wait for all tasks to finish (including the activation task)
        co_return co::kJoin;
      };
    });

    // We run coroutines again, after the main nursery is closed, to finalize
    // all resource tables. This guarantees that all import jobs have completed
    // and no further writes will be made to the tables.
    // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines) - blocking call
    co::Run(*event_loop_, [this]() -> co::Co<> {
      const auto ok = co_await table_registry_->FinalizeAll();
      if (!ok) {
        LOG_F(WARNING, "Resource table finalization failed");
      }
      co_return;
    });

    // Cleanup on import thread (in reverse order of creation)
    if (thread_pool_) {
      thread_pool_.reset();
    }
    if (async_importer_) {
      async_importer_.reset();
    }
    if (table_registry_) {
      table_registry_.reset();
    }
    if (index_registry_) {
      index_registry_.reset();
    }
    if (file_writer_) {
      file_writer_.reset();
    }
    if (file_reader_) {
      file_reader_.reset();
    }
    if (event_loop_) {
      event_loop_.reset();
    }

    thread_running_.store(false, std::memory_order_release);

    DLOG_F(INFO, "Import thread exited");
  }

  //! Initiate shutdown without blocking.
  auto RequestShutdown() -> void
  {
    const bool was_requested
      = shutdown_requested_.exchange(true, std::memory_order_acq_rel);
    if (was_requested) {
      return;
    }

    stop_source_.request_stop();
    DLOG_F(INFO, "Shutdown requested");

    // Trigger all cancel events to cancel pending jobs on the import thread.
    std::vector<std::shared_ptr<co::Event>> events_to_trigger;
    {
      std::scoped_lock lock(cancel_events_mutex_);
      events_to_trigger.reserve(cancel_events_.size());
      for (auto& event : cancel_events_ | std::views::values) {
        if (event) {
          events_to_trigger.push_back(event);
        }
      }
    }

    if (!events_to_trigger.empty()) {
      if (event_loop_) {
        // IMPORTANT: Trigger cancellations on the import thread to keep
        // coroutine resumption on the correct executor.
        event_loop_->Post(
          [events = std::move(events_to_trigger)]() mutable -> void {
            for (auto& event : events) {
              event->Trigger();
            }
          });
      } else {
        for (auto& event : events_to_trigger) {
          event->Trigger();
        }
      }
    }

    // Post the stop request to the event loop to ensure it runs on the
    // correct thread. The nursery will be canceled and co::Run() will
    // exit naturally, causing the event loop to stop.
    if (event_loop_ && async_importer_) {
      event_loop_->Post([this]() -> void { async_importer_->Stop(); });
    }
  }

  //! Shutdown the import thread and wait for completion.
  auto Shutdown() -> void
  {
    std::scoped_lock lock(shutdown_mutex_);
    if (shutdown_complete_.load(std::memory_order_acquire)) {
      return;
    }

    RequestShutdown();

    // Wait for import thread to exit (co::Run() will complete after nursery
    // drains)
    if (import_thread_.joinable()) {
      import_thread_.join();
    }

    shutdown_complete_.store(true, std::memory_order_release);

    DLOG_F(INFO, "Shutdown complete");
  }
};

//=== AsyncImportService Public API
//===-----------------------------------------//

AsyncImportService::AsyncImportService(std::optional<Config> config)
  : impl_(
      std::make_unique<Impl>(config.has_value() ? config.value() : Config {}))
{
  DLOG_F(INFO, "Created with {} thread pool workers",
    impl_->config_.thread_pool_size);

  impl_->StartThread();
}

AsyncImportService::~AsyncImportService()
{
  CHECK_F(IsStopped(),
    "Destroyed without Stop(). "
    "Call Stop() and wait for IsStopped() before destruction.");
}

auto AsyncImportService::SubmitImport(ImportRequest request,
  const ImportCompletionCallback& on_complete,
  const ProgressEventCallback& on_progress,
  const std::optional<ImportConcurrency>& concurrency_override) const
  -> std::optional<ImportJobId>
{
  return SubmitImport(std::move(request), on_complete, on_progress, nullptr,
    concurrency_override);
}

auto AsyncImportService::SubmitImport(ImportRequest request,
  const ImportCompletionCallback& on_complete,
  const ProgressEventCallback& on_progress, const ImportJobFactory& job_factory,
  const std::optional<ImportConcurrency>& concurrency_override) const
  -> std::optional<ImportJobId>
{
  // Check if we're accepting jobs
  if (impl_->shutdown_requested_.load(std::memory_order_acquire)) {
    LOG_F(WARNING, "Submit rejected: service is shutting down");
    return std::nullopt;
  }

  if (!impl_->thread_running_.load(std::memory_order_acquire)) {
    LOG_F(WARNING, "Submit rejected: import thread not running");
    return std::nullopt;
  }

  // Check if the async importer is ready
  if (!impl_->async_importer_ || !impl_->async_importer_->IsAcceptingJobs()) {
    LOG_F(WARNING, "Submit rejected: async importer not ready");
    return std::nullopt;
  }

  // Generate job ID
  const ImportJobId job_id { impl_->next_job_id_.fetch_add(
    1, std::memory_order_relaxed) };

  if (!impl_->file_reader_) {
    LOG_F(WARNING, "Submit rejected: async file reader not ready");
    return std::nullopt;
  }

  if (!impl_->file_writer_) {
    LOG_F(WARNING, "Submit rejected: async file writer not ready");
    return std::nullopt;
  }

  if (!impl_->thread_pool_) {
    LOG_F(WARNING, "Submit rejected: thread pool not ready");
    return std::nullopt;
  }

  const bool use_custom_factory = static_cast<bool>(job_factory);
  auto format = ImportFormat::kUnknown;
  if (!use_custom_factory) {
    format = request.GetFormat();
    if (format == ImportFormat::kUnknown) {
      LOG_F(WARNING, "Submit rejected: unknown format for '{}'",
        request.source_path.string());
      return std::nullopt;
    }
  }

  auto cancel_event = std::make_shared<co::Event>();

  DLOG_F(
    INFO, "Submitting import job {}: {}", job_id, request.source_path.string());

  // Wrap completion callback to clean up cancel event
  auto wrapped_complete = [this, job_id, on_complete](const ImportJobId id,
                            const ImportReport& report) -> void {
    // Clean up cancel event
    {
      std::scoped_lock lock(impl_->cancel_events_mutex_);
      impl_->cancel_events_.erase(job_id);
    }

    // Invoke user callback
    if (on_complete) {
      on_complete(id, report);
    }
  };

  const auto source_path_string = request.source_path.string();

  const auto job_name = request.job_name.value_or(use_custom_factory
      ? std::string("custom:") + nostd::to_string(job_id)
      : MakeJobName(format, job_id, request.source_path));

  const auto file_reader = observer_ptr(impl_->file_reader_.get());
  const auto file_writer = observer_ptr(impl_->file_writer_.get());
  const auto thread_pool = observer_ptr(impl_->thread_pool_.get());
  const auto table_registry = observer_ptr(impl_->table_registry_.get());
  const auto index_registry = observer_ptr(impl_->index_registry_.get());

  const auto& concurrency = concurrency_override.has_value()
    ? *concurrency_override
    : impl_->config_.concurrency;

  detail::ImportJobParams params {
    .id = job_id,
    .request = std::move(request),
    .on_complete = wrapped_complete,
    .on_progress = on_progress,
    .cancel_event = cancel_event,
    .reader = file_reader,
    .writer = file_writer,
    .thread_pool = thread_pool,
    .registry = table_registry,
    .index_registry = index_registry,
    .concurrency = concurrency,
    .stop_token = impl_->stop_source_.get_token(),
  };

  std::shared_ptr<detail::ImportJob> job;
  if (use_custom_factory) {
    job = job_factory(std::move(params));
  } else {
    job = CreateJobForFormat(format, std::move(params));
  }
  if (!job) {
    LOG_F(WARNING, "Submit rejected: failed to create job for '{}'",
      source_path_string);
    return std::nullopt;
  }

  job->SetName(job_name);

  // Store cancel event for CancelJob() support
  {
    std::scoped_lock lock(impl_->cancel_events_mutex_);
    impl_->cancel_events_[job_id] = cancel_event;
  }

  // Create job entry
  detail::JobEntry entry {
    .job_id = job_id,
    .job = std::move(job),
    .cancel_event = cancel_event,
  };

  if (!impl_->async_importer_->CanAcceptJob()) {
    LOG_F(WARNING, "Submit rejected: channel full for job {}", job_id);
    {
      std::scoped_lock lock(impl_->cancel_events_mutex_);
      impl_->cancel_events_.erase(job_id);
    }
    return std::nullopt;
  }

  // Submit directly to AsyncImporter via event loop post
  // The event loop ensures this runs on the import thread
  impl_->event_loop_->Post(
    [importer = impl_->async_importer_.get(), entry = std::move(entry),
      wrapped_complete, source_path = source_path_string]() mutable -> void {
      // Now on import thread - submit to AsyncImporter
      // Use TrySubmitJob since we're not in a coroutine context
      if (!importer->TrySubmitJob(std::move(entry))) {
        LOG_F(WARNING, "Failed to submit job (channel full or closed)");
        ImportReport report {
        .cooked_root = {},
        .source_key = {},
        .diagnostics = {
          {
            .severity = ImportSeverity::kError,
            .code = "import.queue_full",
            .message = "Import queue is full",
            .source_path = source_path,
            .object_path = {},
          },
        },
        .materials_written = 0,
        .geometry_written = 0,
        .scenes_written = 0,
        .success = false,
      };
        wrapped_complete(entry.job_id, report);
      }
    });

  return job_id;
}

auto AsyncImportService::SubmitManifest(const ImportManifest& manifest,
  const ImportCompletionCallback& on_item_complete,
  const ProgressEventCallback& on_progress) const -> std::vector<ImportJobId>
{
  std::vector<ImportJobId> job_ids;
  auto requests = manifest.BuildRequests(std::cerr);
  job_ids.reserve(requests.size());

  for (auto& request : requests) {
    if (auto job_id = SubmitImport(std::move(request), on_item_complete,
          on_progress, {}, manifest.concurrency)) {
      job_ids.push_back(*job_id);
    }
  }

  return job_ids;
}

auto AsyncImportService::CancelJob(const ImportJobId job_id) const -> bool
{
  std::shared_ptr<co::Event> cancel_event;

  // Look up cancel event
  {
    std::scoped_lock lock(impl_->cancel_events_mutex_);
    auto it = impl_->cancel_events_.find(job_id);
    if (it == impl_->cancel_events_.end()) {
      // Job not found (already completed or invalid)
      return false;
    }
    cancel_event = it->second;
  }

  // Trigger cancellation
  // The job's nursery will observe this and cancel accordingly
  if (cancel_event) {
    if (impl_->event_loop_) {
      impl_->event_loop_->Post(
        [event = cancel_event]() -> void { event->Trigger(); });
    } else {
      cancel_event->Trigger();
    }
    DLOG_F(INFO, "Triggered cancellation for job {}", job_id);
    return true;
  }

  return false;
}

auto AsyncImportService::CancelAll() const -> void
{
  std::vector<std::shared_ptr<co::Event>> events_to_trigger;
  size_t cancel_count = 0;

  {
    std::scoped_lock lock(impl_->cancel_events_mutex_);
    events_to_trigger.reserve(impl_->cancel_events_.size());

    for (auto& event : impl_->cancel_events_ | std::views::values) {
      if (event) {
        events_to_trigger.push_back(event);
      }
    }
    cancel_count = events_to_trigger.size();
  }

  // Trigger all cancel events (outside the lock)
  if (impl_->event_loop_) {
    impl_->event_loop_->Post(
      [events = std::move(events_to_trigger)]() mutable -> void {
        for (auto& event : events) {
          event->Trigger();
        }
      });
  } else {
    for (auto& event : events_to_trigger) {
      event->Trigger();
    }
  }

  DLOG_F(INFO, "Triggered cancellation for {} jobs", cancel_count);
}

auto AsyncImportService::RequestShutdown() const -> void
{
  impl_->RequestShutdown();
}

auto AsyncImportService::Stop() const -> void
{
  try {
    impl_->Shutdown();
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Stop failed: {}", ex.what());
    impl_->shutdown_complete_.store(true, std::memory_order_release);
  } catch (...) {
    LOG_F(ERROR, "Stop failed: unknown exception");
    impl_->shutdown_complete_.store(true, std::memory_order_release);
  }
}

auto AsyncImportService::IsStopped() const -> bool
{
  return impl_->shutdown_complete_.load(std::memory_order_acquire);
}

auto AsyncImportService::IsJobActive(const ImportJobId job_id) const -> bool
{
  std::scoped_lock lock(impl_->cancel_events_mutex_);
  return impl_->cancel_events_.contains(job_id);
}

auto AsyncImportService::IsAcceptingJobs() const -> bool
{
  return !impl_->shutdown_requested_.load(std::memory_order_acquire);
}

auto AsyncImportService::ActiveJobCount() const -> size_t
{
  if (impl_->async_importer_) {
    return impl_->async_importer_->ActiveJobCount();
  }
  return 0;
}

auto AsyncImportService::RunningJobCount() const -> size_t
{
  if (impl_->async_importer_) {
    return impl_->async_importer_->RunningJobCount();
  }
  return 0;
}

auto AsyncImportService::PendingJobCount() const -> size_t
{
  if (impl_->async_importer_) {
    return impl_->async_importer_->PendingJobCount();
  }
  return 0;
}

} // namespace oxygen::content::import
