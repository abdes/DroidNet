//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Internal/JobEntry.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import {

class IAsyncFileWriter;
class ResourceTableRegistry;

} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Internal LiveObject that processes import jobs on the import thread.
/*!
 AsyncImporter runs as a LiveObject within the import thread's event loop.
 It receives jobs via a channel, processes them with the sync backend,
 and dispatches results via callbacks.

 ### Lifecycle

 1. Create the AsyncImporter.
 2. Activate via `ActivateAsync()` in a parent nursery.
 3. Call `Run()` to start the job processing loop.
 4. Submit jobs via `SubmitJob()`.
 5. Call `Stop()` to cancel and drain the channel.

 ### Cancellation

 Each job has an associated `co::Event` for cancellation. Cancellation is
 always reported via `on_complete` with a canceled diagnostic.

 @warning Cancellation must be triggered on the import thread's event loop.
          Triggering cancellation from another thread can resume coroutines on
          the wrong executor and lead to hard aborts.

 @see AsyncImportService for the public thread-safe API.
*/
class AsyncImporter final : public co::LiveObject {
public:
  //! Configuration for the importer.
  struct Config {
    //! Capacity of the job channel (backpressure control).
    size_t channel_capacity = 64;

    //! Maximum number of jobs processed concurrently.
    size_t max_in_flight_jobs = 1;

    //! Async file writer used by import sessions.
    IAsyncFileWriter* file_writer = nullptr;

    //! Resource table registry for global aggregation.
    ResourceTableRegistry* table_registry = nullptr;
  };

  //! Construct an importer with the given configuration.
  OXGN_CNTT_API explicit AsyncImporter(Config config);

  OXGN_CNTT_API ~AsyncImporter() override;

  OXYGEN_MAKE_NON_COPYABLE(AsyncImporter)
  OXYGEN_MAKE_NON_MOVABLE(AsyncImporter)

  //=== LiveObject Interface
  //===-------------------------------------------------//

  //! Activate the importer by opening its nursery.
  /*!
   @param started Task started notification for synchronization.
   @return Coroutine that runs until the nursery is closed.
  */
  OXGN_CNTT_NDAPI auto ActivateAsync(co::TaskStarted<> started)
    -> co::Co<> override;

  //! Start the job processing loop.
  /*!
   Must be called after `ActivateAsync()` has started.
   Starts a background task that receives and processes jobs.
  */
  OXGN_CNTT_API void Run() override;

  //! Request cancellation and close the job channel.
  /*!
     Triggers cancellation of the nursery and closes the job channel. The
     processing loop exits after draining and all in-flight jobs report
     completion.

     @note Call `Stop()` on the import thread (via the event loop) to keep
       coroutine resumption on the correct executor.
  */
  OXGN_CNTT_API void Stop() override;

  //! Check if the importer is running (nursery is open).
  OXGN_CNTT_NDAPI auto IsRunning() const -> bool override;

  //=== Job Submission
  //===---------------------------------------------------------//

  //! Submit a job for processing.
  /*!
   @param entry The job entry containing request and callbacks.
   @return A coroutine that completes when the job is queued.

   @note This is an async operation that may suspend if the channel is full.
  */
  OXGN_CNTT_NDAPI auto SubmitJob(JobEntry entry) -> co::Co<>;

  //! Try to submit a job without blocking.
  /*!
   @param entry The job entry containing request and callbacks.
   @return True if the job was queued, false if the channel was full or closed.
  */
  OXGN_CNTT_NDAPI auto TrySubmitJob(JobEntry entry) -> bool;

  //! Check if the importer has capacity for another job.
  OXGN_CNTT_NDAPI auto CanAcceptJob() const noexcept -> bool;

  //! Close the job channel (no more jobs accepted).
  OXGN_CNTT_API void CloseJobChannel();

  //! Check if the job channel is accepting jobs.
  OXGN_CNTT_NDAPI auto IsAcceptingJobs() const -> bool;

  //! Get the number of active jobs (queued + running).
  OXGN_CNTT_NDAPI auto ActiveJobCount() const noexcept -> size_t;

  //! Get the number of jobs currently running.
  OXGN_CNTT_NDAPI auto RunningJobCount() const noexcept -> size_t;

  //! Get the number of jobs queued but not yet running.
  OXGN_CNTT_NDAPI auto PendingJobCount() const noexcept -> size_t;

private:
  //! The job processing loop coroutine.
  [[nodiscard]] auto ProcessJobsLoop() -> co::Co<>;

  //! Process a single job.
  [[nodiscard]] auto ProcessJob(JobEntry entry) -> co::Co<>;

  //! The nursery for background tasks.
  co::Nursery* nursery_ = nullptr;

  //! Channel for receiving job entries.
  co::Channel<JobEntry> job_channel_;

  //! Channel for completed job notifications.
  co::Channel<int> completion_channel_;

  //! Configuration.
  Config config_;

  //! Channel capacity for backpressure checks.
  size_t channel_capacity_ = 0;

  //! Active job count (queued + running).
  std::atomic<size_t> active_jobs_ { 0 };

  //! Current number of jobs in flight (running).
  std::atomic<size_t> running_jobs_ { 0 };
};

} // namespace oxygen::content::import::detail
