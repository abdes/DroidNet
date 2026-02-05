//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <thread>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/ImportConcurrency.h>
#include <Oxygen/Content/Import/ImportManifest.h>
#include <Oxygen/Content/Import/ImportProgress.h>
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
  struct ImportJobParams;
} // namespace detail

//! Factory for creating custom import jobs.
using ImportJobFactory = std::function<std::shared_ptr<detail::ImportJob>(
  detail::ImportJobParams params)>;

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
 diagnostic code "import.canceled".

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
   [](const ProgressEvent& progress) {
     UpdateProgressBar(progress.header.overall_progress);
   });

 // Later, if needed:
 service.CancelJob(job_id);
 ```

 @see ImportRequest, ImportReport, ProgressEvent
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
  OXGN_CNTT_API explicit AsyncImportService(std::optional<Config> config = {});

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
          Cancelled jobs (via `CancelJob()` or `CancelAll()`) complete with
          `report.success = false` and diagnostic code `"import.canceled"`.

          @note Callbacks are invoked on the import thread. If the calling
                thread is monitoring for results and progress, it should
                handle synchronization appropriately.

   @param on_progress Optional progress callback invoked periodically to
          report phase and item updates. Invoked on the same thread as
          `on_complete`. Can be `nullptr`.
   @param concurrency_override Optional overrides for per-pipeline concurrency.
   @return Valid job ID on success, or `std::nullopt` if rejected
           due to: shutdown, importer not ready, unknown file format, or
           internal failure. When `std::nullopt` is returned, callbacks are
           never invoked.

  @see CancelJob, CancelAll, ImportRequest, ImportReport, ProgressEvent
  */
  OXGN_CNTT_NDAPI auto SubmitImport(ImportRequest request,
    const ImportCompletionCallback& on_complete,
    const ProgressEventCallback& on_progress = nullptr,
    const std::optional<ImportConcurrency>& concurrency_override
    = std::nullopt) const -> std::optional<ImportJobId>;

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
   @param concurrency_override Optional overrides for per-pipeline concurrency.
   @return Valid job ID on success, or `std::nullopt` if
           rejected due to shutdown, importer not ready, or factory failure.

  @see CancelJob, CancelAll, ImportRequest, ImportReport, ProgressEvent
  */
  OXGN_CNTT_NDAPI auto SubmitImport(ImportRequest request,
    const ImportCompletionCallback& on_complete,
    const ProgressEventCallback& on_progress,
    const ImportJobFactory& job_factory,
    const std::optional<ImportConcurrency>& concurrency_override
    = std::nullopt) const -> std::optional<ImportJobId>;

  //! Submit a batch of import jobs defined in an ImportManifest object.
  /*!
   Submits each item in the manifest as an individual import job.

   @param manifest The manifest object containing jobs.
   @param on_item_complete Optional callback invoked when each item in the batch
          finishes.
   @param on_progress Optional callback for item-level progress updates.
   @return A list of Job IDs for the items in the manifest. If submission fails
           for all items or the manifest is empty, returns an empty vector.
  */
  OXGN_CNTT_NDAPI auto SubmitManifest(const ImportManifest& manifest,
    const ImportCompletionCallback& on_item_complete = nullptr,
    const ProgressEventCallback& on_progress = nullptr) const
    -> std::vector<ImportJobId>;

  //! Cancel a specific import job. Thread-safe.
  /*!
   If the job is pending, it will be removed from the queue.
   If the job is in-flight, a cancellation will be requested.

   @param job_id The job to cancel.
   @return True if the job was found, false if already completed or invalid.
  */
  OXGN_CNTT_API auto CancelJob(ImportJobId job_id) const -> bool;

  //! Cancel all pending and in-flight imports. Thread-safe.
  /*!
    All jobs will complete with a canceled diagnostic.
  */
  OXGN_CNTT_API auto CancelAll() const -> void;

  //! Request graceful shutdown. Thread-safe.
  /*!
  Signals the import thread to stop accepting new jobs, cancel in-flight
  work, and terminate. Does not block; call `Stop()` to wait.

     @note Cancellation is posted to the import thread to keep coroutine
       resumption on the correct executor.

   After calling this, `SubmitImport` will return `kInvalidJobId`.
  */
  OXGN_CNTT_API auto RequestShutdown() const -> void;

  //! Stop the service and wait for shutdown completion. Thread-safe.
  /*!
   Cancels all pending and in-flight jobs, stops the import thread, and
   blocks until shutdown completes. After this returns, `IsStopped()` is
   guaranteed to be true.

    @note Safe to call multiple times.
  */
  OXGN_CNTT_API auto Stop() const -> void;

  //! Check whether the service has fully stopped. Thread-safe.
  /*!
   Returns true only after `Stop()` has completed (or shutdown has finished
   during teardown).
  */
  OXGN_CNTT_NDAPI auto IsStopped() const -> bool;

  //! Check if a job is still pending or in-flight. Thread-safe.
  /*!
   @param job_id The job to check.
   @return True if the job is active, false if completed/canceled/invalid.
  */
  OXGN_CNTT_NDAPI auto IsJobActive(ImportJobId job_id) const -> bool;

  //! Check if the service is still accepting new jobs. Thread-safe.
  OXGN_CNTT_NDAPI auto IsAcceptingJobs() const -> bool;

  //! Get the total number of active jobs (pending + running).
  OXGN_CNTT_NDAPI auto ActiveJobCount() const -> size_t;

  //! Get the number of jobs currently running.
  OXGN_CNTT_NDAPI auto RunningJobCount() const -> size_t;

  //! Get the number of jobs queued but not yet running.
  OXGN_CNTT_NDAPI auto PendingJobCount() const -> size_t;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::import
