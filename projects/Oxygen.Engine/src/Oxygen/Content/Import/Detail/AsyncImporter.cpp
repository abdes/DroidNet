//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Detail/AsyncImporter.h>
#include <Oxygen/Content/Import/Detail/ImportJob.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/OxCo/Algorithms.h>

namespace oxygen::content::import::detail {

AsyncImporter::AsyncImporter(Config config)
  : job_channel_(config.channel_capacity)
  , completion_channel_(
      config.max_in_flight_jobs == 0 ? 1 : config.max_in_flight_jobs)
  , config_(config)
  , channel_capacity_(config.channel_capacity)
{
  DLOG_F(INFO, "AsyncImporter created with channel capacity {}",
    config.channel_capacity);
  if (config_.max_in_flight_jobs == 0) {
    config_.max_in_flight_jobs = 1;
  }
}

AsyncImporter::~AsyncImporter()
{
  DLOG_IF_F(WARNING, (nursery_ != nullptr),
    "AsyncImporter destroyed while nursery is still open. "
    "Did you forget to call Stop()?");
}

//=== LiveObject Interface
//===-------------------------------------------------//

auto AsyncImporter::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  return OpenNursery(nursery_, std::move(started));
}

void AsyncImporter::Run()
{
  DCHECK_F(nursery_ != nullptr, "Run() called before ActivateAsync()");

  // Start the job processing loop as a background task
  nursery_->Start([this]() -> co::Co<> { co_await ProcessJobsLoop(); });

  DLOG_F(INFO, "AsyncImporter job processing loop started");
}

void AsyncImporter::Stop()
{
  DLOG_F(INFO, "AsyncImporter::Stop() called");

  // Close the channel to stop accepting new jobs and unblock receivers
  job_channel_.Close();
  completion_channel_.Close();

  // Cancel the nursery to stop all background tasks
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }
}

auto AsyncImporter::IsRunning() const -> bool { return nursery_ != nullptr; }

//=== Job Submission
//===---------------------------------------------------------//

auto AsyncImporter::SubmitJob(JobEntry entry) -> co::Co<>
{
  DLOG_F(INFO, "Submitting job {} to channel", entry.job_id);
  co_await job_channel_.Send(std::move(entry));
}

auto AsyncImporter::TrySubmitJob(JobEntry entry) -> bool
{
  if (job_channel_.Closed()) {
    DLOG_F(WARNING, "TrySubmitJob: channel is closed");
    return false;
  }

  if (job_channel_.Full()) {
    DLOG_F(WARNING, "TrySubmitJob: channel is full");
    return false;
  }

  // Use TrySend for non-blocking submission
  const auto ok = job_channel_.TrySend(std::move(entry));
  if (ok) {
    active_jobs_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto AsyncImporter::CanAcceptJob() const noexcept -> bool
{
  if (job_channel_.Closed()) {
    return false;
  }

  const auto active = active_jobs_.load(std::memory_order_acquire);
  return active < channel_capacity_;
}

void AsyncImporter::CloseJobChannel()
{
  DLOG_F(INFO, "Closing job channel");
  job_channel_.Close();
}

auto AsyncImporter::IsAcceptingJobs() const -> bool
{
  return !job_channel_.Closed();
}

//=== Private Implementation
//===-------------------------------------------------//

/*!
 The main job processing loop. Receives jobs from the channel and processes
 them one at a time. Exits when the channel is closed and drained.
*/
auto AsyncImporter::ProcessJobsLoop() -> co::Co<>
{
  DLOG_F(INFO, "ProcessJobsLoop started");

  while (true) {
    while (in_flight_jobs_ >= config_.max_in_flight_jobs) {
      auto completed = co_await completion_channel_.Receive();
      if (!completed.has_value()) {
        DLOG_F(INFO, "Completion channel closed, exiting processing loop");
        co_return;
      }
      if (in_flight_jobs_ > 0) {
        --in_flight_jobs_;
      }
      active_jobs_.fetch_sub(1, std::memory_order_acq_rel);
    }

    // Receive next job (suspends until available or channel closed)
    auto maybe_entry = co_await job_channel_.Receive();

    if (!maybe_entry.has_value()) {
      // Channel closed and empty - exit the loop
      DLOG_F(INFO, "Job channel closed, exiting processing loop");
      break;
    }

    // Process the job
    ++in_flight_jobs_;
    nursery_->Start(
      [this, entry = std::move(*maybe_entry)]() mutable -> co::Co<> {
        co_await ProcessJob(std::move(entry));
        co_await completion_channel_.Send(1);
      });
  }

  while (in_flight_jobs_ > 0) {
    auto completed = co_await completion_channel_.Receive();
    if (!completed.has_value()) {
      break;
    }
    --in_flight_jobs_;
    active_jobs_.fetch_sub(1, std::memory_order_acq_rel);
  }

  DLOG_F(INFO, "ProcessJobsLoop exited");
}

/*!
 Process a single import job. Checks for cancellation, reports progress,
 invokes the sync backend, and dispatches results.

 @param entry The job entry to process.
*/
auto AsyncImporter::ProcessJob(JobEntry entry) -> co::Co<>
{
  if (!entry.job) {
    DLOG_F(ERROR, "ProcessJob received null job for id {}", entry.job_id);
    co_return;
  }

  OXCO_WITH_NURSERY(job_supervisor)
  {
    // Create and start the job first
    auto job = std::move(entry.job);
    ImportJob* job_base = job.get();

    // Activate the job (opens its job nursery) and wait until activation
    // completes so that Run() can safely start tasks in the job nursery.
    co_await job_supervisor.Start(
      [job_base, job](co::TaskStarted<> started) -> co::Co<> {
        co_await job_base->ActivateAsync(std::move(started));
      });

    job_base->Run();

    if (entry.cancel_event) {
      auto [cancelled, waited]
        = co_await co::AnyOf(*entry.cancel_event, job_base->Wait());
      if (cancelled.has_value()) {
        DLOG_F(INFO, "Cancel event triggered, stopping job");
        job_base->Stop();
        co_await job_base->Wait();
      }
    } else {
      co_await job_base->Wait();
    }

    // Either the job completed or was cancelled
    // The nursery will clean up appropriately
    co_return co::kJoin;
  };

  // Note: The job is responsible for reporting cancellation via its
  // completion callback.
}

} // namespace oxygen::content::import::detail
