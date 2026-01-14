//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import::detail {

//! Entry for a single import job in the job channel.
struct JobEntry {
  //! Unique job identifier.
  ImportJobId job_id = kInvalidJobId;

  //! Import request with source path and options.
  ImportRequest request;

  //! Callback for completion notification.
  ImportCompletionCallback on_complete;

  //! Optional callback for progress updates.
  ImportProgressCallback on_progress;

  //! Optional callback for cancellation notification.
  ImportCancellationCallback on_cancel;

  //! Event to signal cancellation request for this job.
  std::shared_ptr<co::Event> cancel_event;
};

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

 Each job has an associated `co::Event` for cancellation. The processing
 loop checks this event using `co::AnyOf()` to allow early exit.

 @see AsyncImportService for the public thread-safe API.
*/
class AsyncImporter final : public co::LiveObject {
public:
  //! Configuration for the importer.
  struct Config {
    //! Capacity of the job channel (backpressure control).
    size_t channel_capacity = 64;
  };

  //! Construct an importer with the given configuration.
  OXGN_CNTT_API explicit AsyncImporter(Config config = {});

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
  OXGN_CNTT_NDAPI [[nodiscard]] auto ActivateAsync(
    co::TaskStarted<> started = {}) -> co::Co<> override;

  //! Start the job processing loop.
  /*!
   Must be called after `ActivateAsync()` has started.
   Starts a background task that receives and processes jobs.
  */
  OXGN_CNTT_API void Run() override;

  //! Request cancellation and close the job channel.
  /*!
   Triggers cancellation of the nursery and closes the channel,
   causing the processing loop to exit after draining.
  */
  OXGN_CNTT_API void Stop() override;

  //! Check if the importer is running (nursery is open).
  OXGN_CNTT_NDAPI [[nodiscard]] auto IsRunning() const -> bool override;

  //=== Job Submission
  //===---------------------------------------------------------//

  //! Submit a job for processing.
  /*!
   @param entry The job entry containing request and callbacks.
   @return A coroutine that completes when the job is queued.

   @note This is an async operation that may suspend if the channel is full.
  */
  OXGN_CNTT_NDAPI [[nodiscard]] auto SubmitJob(JobEntry entry) -> co::Co<>;

  //! Try to submit a job without blocking.
  /*!
   @param entry The job entry containing request and callbacks.
   @return True if the job was queued, false if the channel was full or closed.
  */
  OXGN_CNTT_NDAPI [[nodiscard]] auto TrySubmitJob(JobEntry entry) -> bool;

  //! Close the job channel (no more jobs accepted).
  OXGN_CNTT_API void CloseJobChannel();

  //! Check if the job channel is accepting jobs.
  OXGN_CNTT_NDAPI [[nodiscard]] auto IsAcceptingJobs() const -> bool;

private:
  //! The job processing loop coroutine.
  [[nodiscard]] auto ProcessJobsLoop() -> co::Co<>;

  //! Process a single job.
  [[nodiscard]] auto ProcessJob(JobEntry entry) -> co::Co<>;

  //! The nursery for background tasks.
  co::Nursery* nursery_ = nullptr;

  //! Channel for receiving job entries.
  co::Channel<JobEntry> job_channel_;

  //! Configuration.
  Config config_;
};

} // namespace oxygen::content::import::detail
