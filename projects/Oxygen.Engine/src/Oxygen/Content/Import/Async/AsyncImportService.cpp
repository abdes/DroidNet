//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <latch>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/Async/Detail/AsyncImporter.h>
#include <Oxygen/Content/Import/Async/Detail/ImportJob.h>
#include <Oxygen/Content/Import/Async/IAsyncFileReader.h>
#include <Oxygen/Content/Import/Async/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Async/ImportEventLoop.h>
#include <Oxygen/Content/Import/Async/Jobs/AudioImportJob.h>
#include <Oxygen/Content/Import/Async/Jobs/FbxImportJob.h>
#include <Oxygen/Content/Import/Async/Jobs/GlbImportJob.h>
#include <Oxygen/Content/Import/Async/Jobs/TextureImportJob.h>
#include <Oxygen/Content/Import/Async/ResourceTableRegistry.h>
#include <Oxygen/Content/Import/ImportFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto FormatToString(ImportFormat format) -> std::string_view
  {
    switch (format) {
    case ImportFormat::kFbx:
      return "fbx";
    case ImportFormat::kGltf:
      return "gltf";
    case ImportFormat::kGlb:
      return "glb";
    case ImportFormat::kTextureImage:
      return "texture";
    case ImportFormat::kUnknown:
      return "unknown";
    }
    return "unknown";
  }

  [[nodiscard]] auto MakeJobName(ImportFormat format, ImportJobId job_id,
    const std::filesystem::path& source_path) -> std::string
  {
    const auto file_name = source_path.filename().string();
    const auto name_part = file_name.empty() ? "source" : file_name;
    return std::string(FormatToString(format)) + ":" + std::to_string(job_id)
      + ":" + name_part;
  }

  [[nodiscard]] auto CreateJobForFormat(ImportFormat format, ImportJobId job_id,
    ImportRequest request, ImportCompletionCallback on_complete,
    ImportProgressCallback on_progress, std::shared_ptr<co::Event> cancel_event,
    oxygen::observer_ptr<IAsyncFileReader> file_reader,
    oxygen::observer_ptr<IAsyncFileWriter> file_writer,
    oxygen::observer_ptr<co::ThreadPool> thread_pool,
    oxygen::observer_ptr<ResourceTableRegistry> table_registry)
    -> std::shared_ptr<detail::ImportJob>
  {
    switch (format) {
    case ImportFormat::kFbx:
      return std::make_shared<detail::FbxImportJob>(job_id, std::move(request),
        std::move(on_complete), std::move(on_progress), std::move(cancel_event),
        file_reader, file_writer, thread_pool, table_registry);
    case ImportFormat::kGltf:
      // TODO(Phase 5): Add dedicated GltfImportJob; use GLB job for now.
      return std::make_shared<detail::GlbImportJob>(job_id, std::move(request),
        std::move(on_complete), std::move(on_progress), std::move(cancel_event),
        file_reader, file_writer, thread_pool, table_registry);
    case ImportFormat::kGlb:
      return std::make_shared<detail::GlbImportJob>(job_id, std::move(request),
        std::move(on_complete), std::move(on_progress), std::move(cancel_event),
        file_reader, file_writer, thread_pool, table_registry);
    case ImportFormat::kTextureImage:
      return std::make_shared<detail::TextureImportJob>(job_id,
        std::move(request), std::move(on_complete), std::move(on_progress),
        std::move(cancel_event), file_reader, file_writer, thread_pool,
        table_registry);
    case ImportFormat::kUnknown:
      break;
    }
    return {};
  }

} // namespace

//=== Implementation
//===-------------------------------------------------------//

struct AsyncImportService::Impl {
  explicit Impl(Config config)
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

  //! Thread pool for CPU-bound import work (created on import thread).
  std::unique_ptr<co::ThreadPool> thread_pool_;

  //! Next job ID to assign.
  std::atomic<ImportJobId> next_job_id_ { 1 };

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

  //! Flag indicating the import thread is running and ready.
  std::atomic<bool> thread_running_ { false };

  //! Latch to signal thread startup complete.
  std::latch startup_latch_ { 1 };

  //! Start the import thread and wait for it to be ready.
  auto StartThread() -> void
  {
    import_thread_ = std::thread([this]() { ThreadMain(); });

    // Wait for the import thread to finish initialization
    startup_latch_.wait();
  }

  [[nodiscard]] static auto ToLowerAscii(std::string value) -> std::string
  {
    std::transform(
      value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
      });
    return value;
  }

  [[nodiscard]] static auto DetectFormatFromPath(
    const std::filesystem::path& path) -> ImportFormat
  {
    const auto ext = ToLowerAscii(path.extension().string());

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga"
      || ext == ".bmp" || ext == ".psd" || ext == ".gif" || ext == ".hdr"
      || ext == ".pic" || ext == ".ppm" || ext == ".pgm" || ext == ".pnm"
      || ext == ".exr") {
      return ImportFormat::kTextureImage;
    }
    if (ext == ".gltf") {
      return ImportFormat::kGltf;
    }
    if (ext == ".glb") {
      return ImportFormat::kGlb;
    }
    if (ext == ".fbx") {
      return ImportFormat::kFbx;
    }

    return ImportFormat::kUnknown;
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

    // Create thread pool for CPU-bound work (pipelines, mesh processing)
    thread_pool_ = std::make_unique<co::ThreadPool>(
      *event_loop_, config_.thread_pool_size);

    // Create the async importer
    async_importer_
      = std::make_unique<detail::AsyncImporter>(detail::AsyncImporter::Config {
        .channel_capacity = 64,
        .max_in_flight_jobs = config_.max_in_flight_jobs,
        .file_writer = file_writer_.get(),
        .table_registry = table_registry_.get(),
      });

    thread_running_.store(true, std::memory_order_release);

    // Signal that initialization is complete
    startup_latch_.count_down();

    // Run the coroutine runtime with the AsyncImporter
    oxygen::co::Run(*event_loop_, [this]() -> co::Co<> {
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

    if (table_registry_) {
      oxygen::co::Run(*event_loop_, [this]() -> co::Co<> {
        const auto ok = co_await table_registry_->FinalizeAll();
        if (!ok) {
          DLOG_F(
            WARNING, "AsyncImportService: resource table finalization failed");
        }
        co_return;
      });
    }

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

  //! Shutdown the import thread.
  auto Shutdown() -> void
  {
    // Check if we've already completed shutdown
    if (shutdown_complete_.exchange(true, std::memory_order_acq_rel)) {
      return; // Already shut down
    }

    // Mark shutdown requested (in case it wasn't already)
    shutdown_requested_.store(true, std::memory_order_release);

    DLOG_F(INFO, "AsyncImportService shutting down");

    // Trigger all cancel events
    {
      std::lock_guard lock(cancel_events_mutex_);
      for (auto& [id, event] : cancel_events_) {
        if (event) {
          event->Trigger();
        }
      }
    }

    // Post the stop request to the event loop to ensure it runs on the
    // correct thread. The nursery will be cancelled and co::Run() will
    // exit naturally, causing the event loop to stop.
    if (event_loop_ && async_importer_) {
      event_loop_->Post([this]() { async_importer_->Stop(); });
    }

    // Wait for import thread to exit (co::Run() will complete after nursery
    // drains)
    if (import_thread_.joinable()) {
      import_thread_.join();
    }

    DLOG_F(INFO, "AsyncImportService shutdown complete");
  }
};

//=== AsyncImportService Public API
//===-----------------------------------------//

AsyncImportService::AsyncImportService(Config config)
  : impl_(std::make_unique<Impl>(config))
{
  DLOG_F(INFO, "AsyncImportService created with {} thread pool workers",
    config.thread_pool_size);

  impl_->StartThread();
}

AsyncImportService::~AsyncImportService() { impl_->Shutdown(); }

auto AsyncImportService::SubmitImport(ImportRequest request,
  ImportCompletionCallback on_complete, ImportProgressCallback on_progress)
  -> ImportJobId
{
  return SubmitImport(
    std::move(request), std::move(on_complete), std::move(on_progress), {});
}

auto AsyncImportService::SubmitImport(ImportRequest request,
  ImportCompletionCallback on_complete, ImportProgressCallback on_progress,
  ImportJobFactory job_factory) -> ImportJobId
{
  // Check if we're accepting jobs
  if (impl_->shutdown_requested_.load(std::memory_order_acquire)) {
    DLOG_F(WARNING, "SubmitImport: service is shutting down");
    return kInvalidJobId;
  }

  // Check if the async importer is ready
  if (!impl_->async_importer_ || !impl_->async_importer_->IsAcceptingJobs()) {
    DLOG_F(WARNING, "SubmitImport: async importer not ready");
    return kInvalidJobId;
  }

  // Generate job ID
  const auto job_id
    = impl_->next_job_id_.fetch_add(1, std::memory_order_relaxed);

  if (!impl_->file_reader_) {
    DLOG_F(WARNING, "SubmitImport: async file reader not ready");
    return kInvalidJobId;
  }

  if (!impl_->file_writer_) {
    DLOG_F(WARNING, "SubmitImport: async file writer not ready");
    return kInvalidJobId;
  }

  if (!impl_->thread_pool_) {
    DLOG_F(WARNING, "SubmitImport: thread pool not ready");
    return kInvalidJobId;
  }

  const bool use_custom_factory = static_cast<bool>(job_factory);
  auto format = ImportFormat::kUnknown;
  if (!use_custom_factory) {
    format = Impl::DetectFormatFromPath(request.source_path);
    if (format == ImportFormat::kUnknown) {
      DLOG_F(WARNING, "SubmitImport: unknown format for '{}'",
        request.source_path.string());
      return kInvalidJobId;
    }
  }

  std::shared_ptr<co::Event> cancel_event = std::make_shared<co::Event>();

  DLOG_F(
    INFO, "Submitting import job {}: {}", job_id, request.source_path.string());

  // Wrap completion callback to clean up cancel event
  auto wrapped_complete
    = [this, job_id, on_complete](ImportJobId id, const ImportReport& report) {
        // Clean up cancel event
        {
          std::lock_guard lock(impl_->cancel_events_mutex_);
          impl_->cancel_events_.erase(job_id);
        }

        // Invoke user callback
        if (on_complete) {
          on_complete(id, report);
        }
      };

  const auto source_path_string = request.source_path.string();

  const auto job_name = request.job_name.value_or(use_custom_factory
      ? std::string("custom:") + std::to_string(job_id)
      : MakeJobName(format, job_id, request.source_path));

  const auto file_reader
    = oxygen::observer_ptr<IAsyncFileReader>(impl_->file_reader_.get());
  const auto file_writer
    = oxygen::observer_ptr<IAsyncFileWriter>(impl_->file_writer_.get());
  const auto thread_pool
    = oxygen::observer_ptr<co::ThreadPool>(impl_->thread_pool_.get());
  const auto table_registry
    = oxygen::observer_ptr<ResourceTableRegistry>(impl_->table_registry_.get());

  std::shared_ptr<detail::ImportJob> job;
  if (use_custom_factory) {
    job = job_factory(job_id, std::move(request), std::move(wrapped_complete),
      std::move(on_progress), cancel_event, file_reader, file_writer,
      thread_pool, table_registry);
  } else {
    job = CreateJobForFormat(format, job_id, std::move(request),
      std::move(wrapped_complete), std::move(on_progress), cancel_event,
      file_reader, file_writer, thread_pool, table_registry);
  }
  if (!job) {
    DLOG_F(WARNING, "SubmitImport: failed to create job for '{}'",
      source_path_string);
    return kInvalidJobId;
  }

  job->SetName(job_name);

  // Store cancel event for CancelJob() support
  {
    std::lock_guard lock(impl_->cancel_events_mutex_);
    impl_->cancel_events_[job_id] = cancel_event;
  }

  // Create job entry
  detail::JobEntry entry {
    .job_id = job_id,
    .job = std::move(job),
    .cancel_event = cancel_event,
  };

  if (!impl_->async_importer_->CanAcceptJob()) {
    DLOG_F(
      WARNING, "SubmitImport: AsyncImporter channel full for job {}", job_id);
    {
      std::lock_guard lock(impl_->cancel_events_mutex_);
      impl_->cancel_events_.erase(job_id);
    }
    return kInvalidJobId;
  }

  const auto source_path = source_path_string;
  auto on_submit_failed = wrapped_complete;

  // Submit directly to AsyncImporter via event loop post
  // The event loop ensures this runs on the import thread
  impl_->event_loop_->Post(
    [importer = impl_->async_importer_.get(), entry = std::move(entry),
      on_submit_failed, source_path]() mutable {
      // Now on import thread - submit to AsyncImporter
      // Use TrySubmitJob since we're not in a coroutine context
      if (!importer->TrySubmitJob(std::move(entry))) {
        DLOG_F(WARNING,
          "Failed to submit job to AsyncImporter (channel full or closed)");
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
        on_submit_failed(entry.job_id, report);
      }
    });

  return job_id;
}

auto AsyncImportService::CancelJob(ImportJobId job_id) -> bool
{
  if (job_id == kInvalidJobId) {
    return false;
  }

  std::shared_ptr<co::Event> cancel_event;

  // Look up cancel event
  {
    std::lock_guard lock(impl_->cancel_events_mutex_);
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
      impl_->event_loop_->Post([event = cancel_event]() { event->Trigger(); });
    } else {
      cancel_event->Trigger();
    }
    DLOG_F(INFO, "Triggered cancellation for job {}", job_id);
    return true;
  }

  return false;
}

auto AsyncImportService::CancelAll() -> void
{
  std::vector<std::shared_ptr<co::Event>> events_to_trigger;
  size_t cancel_count = 0;

  {
    std::lock_guard lock(impl_->cancel_events_mutex_);
    events_to_trigger.reserve(impl_->cancel_events_.size());

    for (auto& [id, event] : impl_->cancel_events_) {
      if (event) {
        events_to_trigger.push_back(event);
      }
    }
    cancel_count = events_to_trigger.size();
  }

  // Trigger all cancel events (outside the lock)
  if (impl_->event_loop_) {
    impl_->event_loop_->Post([events = std::move(events_to_trigger)]() mutable {
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

auto AsyncImportService::RequestShutdown() -> void
{
  impl_->shutdown_requested_.store(true, std::memory_order_release);

  DLOG_F(INFO, "Shutdown requested (non-blocking)");
}

auto AsyncImportService::IsJobActive(ImportJobId job_id) const -> bool
{
  if (job_id == kInvalidJobId) {
    return false;
  }

  std::lock_guard lock(impl_->cancel_events_mutex_);
  return impl_->cancel_events_.contains(job_id);
}

auto AsyncImportService::IsAcceptingJobs() const -> bool
{
  return !impl_->shutdown_requested_.load(std::memory_order_acquire);
}

auto AsyncImportService::PendingJobCount() const -> size_t
{
  // Note: We can't distinguish between pending vs in-flight without
  // inspecting AsyncImporter's channel state. Return total active count.
  std::lock_guard lock(impl_->cancel_events_mutex_);
  return impl_->cancel_events_.size();
}

auto AsyncImportService::InFlightJobCount() const -> size_t
{
  // Return total active job count (pending + in-flight)
  std::lock_guard lock(impl_->cancel_events_mutex_);
  return impl_->cancel_events_.size();
}

} // namespace oxygen::content::import
