//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::co {
class Event;
class ThreadPool;
} // namespace oxygen::co

namespace oxygen::content::import {

class IAsyncFileReader;
class IAsyncFileWriter;
class ResourceTableRegistry;

namespace detail {
  class ImportJob;
} // namespace detail

//! Unique identifier for an import job.
using ImportJobId = uint64_t;

//! Invalid job ID constant.
inline constexpr ImportJobId kInvalidJobId = 0;

//! Current phase of the import process.
enum class ImportPhase : uint8_t {
  kPending, //!< Job queued, not started.
  kParsing, //!< Reading/parsing source file.
  kTextures, //!< Cooking textures.
  kMaterials, //!< Processing materials.
  kGeometry, //!< Processing geometry.
  kScene, //!< Building scene graph.
  kWriting, //!< Writing cooked output.
  kComplete, //!< Finished.
  kCancelled, //!< Cancelled by user.
  kFailed, //!< Failed with error.
};

//! Progress update for UI integration.
struct ImportProgress {
  //! Job this progress applies to.
  ImportJobId job_id = kInvalidJobId;

  //! Current phase of import.
  ImportPhase phase = ImportPhase::kPending;

  //! Progress within current phase (0.0 - 1.0).
  float phase_progress = 0.0f;

  //! Overall progress (0.0 - 1.0).
  float overall_progress = 0.0f;

  //! Human-readable status message.
  std::string message;

  //! Items processed in current phase.
  uint32_t items_completed = 0;
  uint32_t items_total = 0;

  //! Incremental diagnostics (warnings/errors as they occur).
  std::vector<ImportDiagnostic> new_diagnostics;
};

//! Completion callback invoked when import finishes.
/*!
 @param job_id The job that completed.
 @param report The import result.

 @note Invoked on the thread that called SubmitImport, if that thread
       has an event loop. Otherwise, invoked on the import thread.
*/
using ImportCompletionCallback
  = std::function<void(ImportJobId, const ImportReport&)>;

//! Progress callback for UI updates.
/*!
 @param progress Current progress information.

 @note Invoked on the thread that called SubmitImport, if that thread
       has an event loop. Otherwise, invoked on the import thread.
*/
using ImportProgressCallback = std::function<void(const ImportProgress&)>;

//! Factory for creating custom import jobs.
//! Per-pipeline concurrency settings.
struct ImportPipelineConcurrency {
  //! Number of worker coroutines to start for the pipeline.
  uint32_t workers = 2;

  //! Bounded capacity of the pipeline work queues.
  uint32_t queue_capacity = 64;
};

//! Concurrency tuning for async import pipelines.
struct ImportConcurrency {
  ImportPipelineConcurrency texture { .workers = 2, .queue_capacity = 64 };
  ImportPipelineConcurrency buffer { .workers = 2, .queue_capacity = 64 };
  ImportPipelineConcurrency material { .workers = 2, .queue_capacity = 64 };
  ImportPipelineConcurrency geometry { .workers = 2, .queue_capacity = 32 };
  ImportPipelineConcurrency scene { .workers = 1, .queue_capacity = 8 };
};

//! Factory for creating custom import jobs.
using ImportJobFactory = std::function<std::shared_ptr<detail::ImportJob>(
  ImportJobId, ImportRequest, ImportCompletionCallback, ImportProgressCallback,
  std::shared_ptr<co::Event>, oxygen::observer_ptr<IAsyncFileReader>,
  oxygen::observer_ptr<IAsyncFileWriter>, oxygen::observer_ptr<co::ThreadPool>,
  oxygen::observer_ptr<ResourceTableRegistry>, const ImportConcurrency&)>;

//! Thread-safe service for submitting async import jobs.
/*!
 AsyncImportService manages a dedicated import thread with its own event
 loop and ThreadPool. All public methods are thread-safe and can be called
 from any thread.

 ### Architecture

 Jobs are submitted via a thread-safe channel directly to AsyncImporter,
 which processes them sequentially on the import thread. The service
 tracks active jobs only for cancellation support.

 ### Lifecycle

 1. Construct the service (spawns import thread).
 2. Call `SubmitImport()` from any thread to queue jobs.
 3. Receive callbacks on your thread (via ThreadNotification if available).
 4. Call `Stop()` and wait for `IsStopped()` before destruction.

 ### Shutdown Contract

 - Call `RequestShutdown()` to stop accepting new jobs.
 - Call `Stop()` to cancel in-flight work and wait for shutdown to finish.
 - Destroy the service only after `IsStopped()` returns true.

 @warning Cancellation must be triggered on the import thread's event loop.
          Triggering cancellation from another thread can resume coroutines on
          the wrong executor and lead to hard aborts.

 ### Cancellation

 Per-job cancellation is supported via `CancelJob(job_id)`, which triggers
 an event observed by the job's nursery. Cancelled jobs complete with a
 diagnostic code "import.cancelled".

 ### Simplified Design (2026-01-15 Refactoring)

 Previous implementation tracked jobs in multiple queues with manual
 synchronization. This was redundant with co::Channel. Current design:

 - Jobs submitted directly to AsyncImporter via channel
 - No pending/active/completed tracking (channel handles state)
 - Only cancel events tracked for cancellation support
 - Nursery-based cancellation (no manual state machines)

 ### Thread Safety

 All public methods are thread-safe. The service internally marshals
 requests to the import thread and results back to the caller's thread.

 ### Callback Threading

 Callbacks are invoked on the caller's thread if that thread has an
 event loop with `ThreadNotification` support. For threads without an
 event loop (e.g., main thread before starting), callbacks are invoked
 directly on the import thread.

 ### Example

 ```cpp
 AsyncImportService service;
 auto job_id = service.SubmitImport(
   ImportRequest{.source_path = "model.gltf"},
   [](ImportJobId id, const ImportReport& report) {
     if (report.success) {
       LOG_F(INFO, "Import {} succeeded", id);
     }
   },
   [](const ImportProgress& progress) {
     UpdateProgressBar(progress.overall_progress);
   });

 // Later, if needed:
 service.CancelJob(job_id);
 ```

 @see ImportRequest, ImportReport, ImportProgress
*/
class AsyncImportService final {
public:
  //! Configuration for the import service.
  struct Config {
    //! Number of worker threads in the import ThreadPool.
    uint32_t thread_pool_size = std::thread::hardware_concurrency();

    //! Maximum number of jobs processed concurrently.
    uint32_t max_in_flight_jobs = std::thread::hardware_concurrency();

    //! Per-pipeline concurrency settings (workers and queue capacity).
    ImportConcurrency concurrency {};
  };

  //! Construct and start the import thread.
  /*!
   @param config Service configuration.
  */
  OXGN_CNTT_API explicit AsyncImportService(Config config = {});

  //! Verify the service was stopped before destruction.
  /*!
   The service must be stopped explicitly by calling `Stop()` and waiting
   for `IsStopped()` before destruction. Destruction without a prior stop
   will abort the program.
  */
  OXGN_CNTT_API ~AsyncImportService();

  OXYGEN_MAKE_NON_COPYABLE(AsyncImportService)
  OXYGEN_MAKE_NON_MOVABLE(AsyncImportService)

  //! Submit an import job for asynchronous processing.
  /*!
   Detects the asset format from the file extension, creates the appropriate
   job instance, and submits it to the import thread. Returns immediately
   while the job executes asynchronously.

   @param request Import request specifying source path, optional cooked output
          root, and optional job name. File extension determines the format
          (`.fbx`, `.gltf`, `.glb`). If `cooked_root` is omitted, a default
          location adjacent to the source file is used. The optional `job_name`
          overrides the default naming (`format:id:filename`).
   @param on_complete Completion callback invoked exactly once when the job
          finishes (success, failure, or cancellation). Receives the job ID and
          `ImportReport` with success status, diagnostics, and asset metadata.
          Invoked on the calling thread if it has an event loop with
          `ThreadNotification` support; otherwise on the import thread.
          Cancelled jobs (via `CancelJob()` or `CancelAll()`) complete with
          `report.success = false` and diagnostic code `"import.cancelled"`.
   @param on_progress Optional progress callback invoked periodically to report
          phase transitions and progress percentages. Invoked on the same thread
          as `on_complete`. Can be `nullptr`.
   @return Valid job ID (`> 0`) on success, or `kInvalidJobId` (`0`) if rejected
           due to: shutdown, importer not ready, unknown file format, or
           internal failure. When `kInvalidJobId` is returned, callbacks are
           never invoked.

   @see CancelJob, CancelAll, ImportRequest, ImportReport, ImportProgress
  */
  OXGN_CNTT_NDAPI auto SubmitImport(ImportRequest request,
    ImportCompletionCallback on_complete,
    ImportProgressCallback on_progress = nullptr) -> ImportJobId;

  //! Submit a custom import job for asynchronous processing.
  /*!
   Creates the job using the provided factory and submits it to the import
   thread. This overload bypasses file-extension detection and allows custom
   or test-specific jobs to run through the same cancellation and callback
   pipeline.

   @param request Import request used by the custom job.
   @param on_complete Completion callback invoked exactly once when the job
          finishes.
   @param on_progress Optional progress callback invoked periodically.
   @param job_factory Factory invoked to construct the job instance.
   @return Valid job ID (`> 0`) on success, or `kInvalidJobId` (`0`) if
           rejected due to shutdown, importer not ready, or factory failure.

   @see CancelJob, CancelAll, ImportRequest, ImportReport, ImportProgress
  */
  OXGN_CNTT_NDAPI auto SubmitImport(ImportRequest request,
    ImportCompletionCallback on_complete, ImportProgressCallback on_progress,
    ImportJobFactory job_factory) -> ImportJobId;

  //! Cancel a specific import job. Thread-safe.
  /*!
   If the job is pending, it will be removed from the queue.
   If the job is in-flight, a cancellation will be requested.

   @param job_id The job to cancel.
   @return True if the job was found, false if already completed or invalid.
  */
  OXGN_CNTT_API auto CancelJob(ImportJobId job_id) -> bool;

  //! Cancel all pending and in-flight imports. Thread-safe.
  /*!
    All jobs will complete with a cancelled diagnostic.
  */
  OXGN_CNTT_API auto CancelAll() -> void;

  //! Request graceful shutdown. Thread-safe.
  /*!
  Signals the import thread to stop accepting new jobs, cancel in-flight
  work, and terminate. Does not block; call `Stop()` to wait.

     @note Cancellation is posted to the import thread to keep coroutine
       resumption on the correct executor.

   After calling this, `SubmitImport` will return `kInvalidJobId`.
  */
  OXGN_CNTT_API auto RequestShutdown() -> void;

  //! Stop the service and wait for shutdown completion. Thread-safe.
  /*!
   Cancels all pending and in-flight jobs, stops the import thread, and
   blocks until shutdown completes. After this returns, `IsStopped()` is
   guaranteed to be true.

    @note Safe to call multiple times.
  */
  OXGN_CNTT_API auto Stop() -> void;

  //! Check whether the service has fully stopped. Thread-safe.
  /*!
   Returns true only after `Stop()` has completed (or shutdown has finished
   during teardown).
  */
  OXGN_CNTT_NDAPI auto IsStopped() const -> bool;

  //! Check if a job is still pending or in-flight. Thread-safe.
  /*!
   @param job_id The job to check.
   @return True if the job is active, false if completed/cancelled/invalid.
  */
  OXGN_CNTT_NDAPI auto IsJobActive(ImportJobId job_id) const -> bool;

  //! Check if the service is still accepting new jobs. Thread-safe.
  OXGN_CNTT_NDAPI auto IsAcceptingJobs() const -> bool;

  //! Get the number of active jobs (pending or in-flight). Thread-safe.
  /*!
   Note: The current implementation cannot distinguish between pending
   (queued) and in-flight (executing) jobs without exposing AsyncImporter
   internals. Both methods return the total count of active jobs.

   @return Number of jobs that have been submitted but not yet completed.
  */
  OXGN_CNTT_NDAPI auto PendingJobCount() const -> size_t;

  //! Get the number of active jobs (same as PendingJobCount). Thread-safe.
  /*!
   @return Number of jobs that have been submitted but not yet completed.
  */
  OXGN_CNTT_NDAPI auto InFlightJobCount() const -> size_t;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::import
