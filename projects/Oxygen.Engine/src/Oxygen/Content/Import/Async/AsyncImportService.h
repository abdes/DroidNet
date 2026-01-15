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
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

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
 4. Destructor blocks until all work is complete.

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

    //! Number of parallel texture cooking tasks.
    uint32_t texture_pipeline_workers = 2;

    //! Bounded capacity for texture work queue (backpressure).
    uint32_t texture_queue_capacity = 64;
  };

  //! Construct and start the import thread.
  /*!
   @param config Service configuration.
  */
  OXGN_CNTT_API explicit AsyncImportService(Config config = {});

  //! Shutdown and join the import thread.
  /*!
   Blocks until all pending work is cancelled and the import thread exits.
    In-flight jobs will complete with a cancelled diagnostic.
  */
  OXGN_CNTT_API ~AsyncImportService();

  OXYGEN_MAKE_NON_COPYABLE(AsyncImportService)
  OXYGEN_MAKE_NON_MOVABLE(AsyncImportService)

  //! Submit an import job. Thread-safe.
  /*!
   @param request     Import request (source path, options, etc.)
   @param on_complete Callback invoked when import finishes.
   @param on_progress Optional callback for progress updates.
   @return Job ID for tracking/cancellation.

   @note All callbacks are invoked on the thread that called SubmitImport,
         provided that thread has an event loop with ThreadNotification.
         For threads without an event loop, callbacks run on import thread.
  */
  OXGN_CNTT_NDAPI auto SubmitImport(ImportRequest request,
    ImportCompletionCallback on_complete,
    ImportProgressCallback on_progress = nullptr) -> ImportJobId;

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
   work, and terminate. Does not block; destructor blocks for completion.

   After calling this, `SubmitImport` will return `kInvalidJobId`.
  */
  OXGN_CNTT_API auto RequestShutdown() -> void;

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
