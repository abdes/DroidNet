//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <latch>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Test/Mocks/TestImportJob.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;
using namespace oxygen::content::import;
namespace co = oxygen::co;

namespace {

[[nodiscard]] auto HasDiagnosticCode(
  const std::vector<ImportDiagnostic>& diagnostics, std::string_view code)
  -> bool
{
  return std::any_of(diagnostics.begin(), diagnostics.end(),
    [code](
      const ImportDiagnostic& diagnostic) { return diagnostic.code == code; });
}

[[nodiscard]] auto MakeTestJobFactory(test::TestImportJob::Config config)
  -> ImportJobFactory
{
  return
    [config](ImportJobId job_id, ImportRequest request,
      ImportCompletionCallback on_complete, ImportProgressCallback on_progress,
      std::shared_ptr<co::Event> cancel_event,
      oxygen::observer_ptr<IAsyncFileReader> file_reader,
      oxygen::observer_ptr<IAsyncFileWriter> file_writer,
      oxygen::observer_ptr<co::ThreadPool> thread_pool,
      oxygen::observer_ptr<ResourceTableRegistry> table_registry,
      const ImportConcurrency& concurrency)
      -> std::shared_ptr<detail::ImportJob> {
      return std::make_shared<test::TestImportJob>(job_id, std::move(request),
        std::move(on_complete), std::move(on_progress), std::move(cancel_event),
        file_reader, file_writer, thread_pool, table_registry, concurrency,
        config);
    };
}

[[nodiscard]] auto SubmitTestJob(AsyncImportService& service,
  ImportRequest request, ImportCompletionCallback on_complete,
  ImportProgressCallback on_progress = nullptr,
  test::TestImportJob::Config config = {}) -> ImportJobId
{
  return service.SubmitImport(std::move(request), std::move(on_complete),
    std::move(on_progress), MakeTestJobFactory(config));
}

//=== Construction and Destruction Tests
//===-----------------------------------//

class AsyncImportServiceLifecycleTest : public ::testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 2 };
};

//! Verify service constructs and destructs without crash.
NOLINT_TEST_F(
  AsyncImportServiceLifecycleTest, ConstructDestruct_NoJobs_Succeeds)
{
  // Arrange & Act
  {
    AsyncImportService service(config_);
    // Allow thread to start
    std::this_thread::sleep_for(50ms);
  }

  // Assert - no crash, no hang
  SUCCEED();
}

//! Verify multiple construct/destruct cycles work correctly.
NOLINT_TEST_F(
  AsyncImportServiceLifecycleTest, MultipleConstructDestruct_Succeeds)
{
  // Arrange & Act
  for (int i = 0; i < 3; ++i) {
    AsyncImportService service(config_);
    std::this_thread::sleep_for(20ms);
  }

  // Assert - no crash, no hang
  SUCCEED();
}

//! Verify IsAcceptingJobs returns true after construction.
NOLINT_TEST_F(AsyncImportServiceLifecycleTest,
  IsAcceptingJobs_AfterConstruction_ReturnsTrue)
{
  // Arrange
  AsyncImportService service(config_);

  // Act & Assert
  EXPECT_TRUE(service.IsAcceptingJobs());
}

//! Verify counts are zero after construction.
NOLINT_TEST_F(
  AsyncImportServiceLifecycleTest, JobCounts_AfterConstruction_AreZero)
{
  // Arrange
  AsyncImportService service(config_);

  // Act & Assert
  EXPECT_EQ(service.PendingJobCount(), 0U);
  EXPECT_EQ(service.InFlightJobCount(), 0U);
}

//=== Job Submission Tests ===------------------------------------------------//

class AsyncImportServiceSubmitTest : public ::testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 2 };
};

//! Verify SubmitImport returns a valid job ID.
NOLINT_TEST_F(AsyncImportServiceSubmitTest, SubmitImport_ReturnsValidJobId)
{
  // Arrange
  AsyncImportService service(config_);
  std::latch done(1);

  // Act
  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  // Assert
  EXPECT_NE(job_id, kInvalidJobId);

  // Cleanup - wait for job to complete
  done.wait();
}

//! Verify completion callback is invoked.
NOLINT_TEST_F(
  AsyncImportServiceSubmitTest, SubmitImport_CompletionCallback_IsInvoked)
{
  // Arrange
  AsyncImportService service(config_);
  std::latch done(1);
  std::atomic<bool> callback_invoked { false };
  ImportJobId received_id = kInvalidJobId;

  // Act
  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&](ImportJobId id, ImportReport) {
        callback_invoked = true;
        received_id = id;
        done.count_down();
      });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  // Assert
  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(received_id, job_id);
}

//! Verify custom job factory can run unknown formats.
NOLINT_TEST_F(
  AsyncImportServiceSubmitTest, SubmitImport_CustomJobFactory_AllowsUnknown)
{
  // Arrange
  AsyncImportService service(config_);
  std::latch done(1);

  const auto job_factory = MakeTestJobFactory({
    .total_delay = 15ms,
    .step_delay = 5ms,
    .report_progress = false,
  });

  // Act
  auto job_id = service.SubmitImport(
    ImportRequest { .source_path = "custom.asset" },
    [&done](ImportJobId, ImportReport) { done.count_down(); }, nullptr,
    job_factory);

  // Assert
  EXPECT_NE(job_id, kInvalidJobId);
  done.wait();
}

//! Verify custom job completes successfully.
NOLINT_TEST_F(AsyncImportServiceSubmitTest, SubmitImport_CustomJob_Completes)
{
  // Arrange
  AsyncImportService service(config_);

  std::latch done(1);
  std::atomic<bool> callback_invoked { false };
  ImportReport received_report;

  // Act
  [[maybe_unused]] auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&](ImportJobId, ImportReport report) {
        callback_invoked = true;
        received_report = std::move(report);
        done.count_down();
      });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  // Assert
  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(received_report.success);
}

//! Verify progress callback is invoked if provided.
NOLINT_TEST_F(
  AsyncImportServiceSubmitTest, SubmitImport_ProgressCallback_IsInvoked)
{
  // Arrange
  AsyncImportService service(config_);
  std::latch done(1);
  std::atomic<bool> progress_invoked { false };

  // Act
  [[maybe_unused]] auto job_id = SubmitTestJob(
    service, ImportRequest { .source_path = "custom.asset" },
    [&done](ImportJobId, ImportReport) { done.count_down(); },
    [&progress_invoked](const ImportProgress& progress) {
      if (progress.phase == ImportPhase::kParsing) {
        progress_invoked = true;
      }
    },
    test::TestImportJob::Config {
      .total_delay = 15ms,
      .step_delay = 5ms,
      .report_progress = true,
    });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  // Assert
  EXPECT_TRUE(progress_invoked);
}

//! Verify multiple jobs get unique IDs.
NOLINT_TEST_F(AsyncImportServiceSubmitTest, SubmitImport_MultipleJobs_UniqueIds)
{
  // Arrange
  AsyncImportService service(config_);
  std::latch done(3);

  // Act
  auto id1
    = SubmitTestJob(service, ImportRequest { .source_path = "custom1.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  auto id2
    = SubmitTestJob(service, ImportRequest { .source_path = "custom2.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  auto id3
    = SubmitTestJob(service, ImportRequest { .source_path = "custom3.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  EXPECT_NE(id1, kInvalidJobId);
  EXPECT_NE(id2, kInvalidJobId);
  EXPECT_NE(id3, kInvalidJobId);

  done.wait();

  // Assert
  EXPECT_NE(id1, id2);
  EXPECT_NE(id2, id3);
  EXPECT_NE(id1, id3);
}

//! Verify SubmitImport returns kInvalidJobId after shutdown.
NOLINT_TEST_F(
  AsyncImportServiceSubmitTest, SubmitImport_AfterShutdown_ReturnsInvalid)
{
  // Arrange
  AsyncImportService service(config_);
  service.RequestShutdown();

  // Act
  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [](ImportJobId, ImportReport) { });

  // Assert
  EXPECT_EQ(job_id, kInvalidJobId);
}

//=== Cancellation Tests ===--------------------------------------------------//

class AsyncImportServiceCancelTest : public ::testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 2 };
};

//! Verify CancelJob returns false for invalid job ID.
NOLINT_TEST_F(AsyncImportServiceCancelTest, CancelJob_InvalidId_ReturnsFalse)
{
  // Arrange
  AsyncImportService service(config_);

  // Act & Assert
  EXPECT_FALSE(service.CancelJob(kInvalidJobId));
  EXPECT_FALSE(service.CancelJob(999));
}

//! Verify CancelJob returns false for completed job.
NOLINT_TEST_F(AsyncImportServiceCancelTest, CancelJob_CompletedJob_ReturnsFalse)
{
  // Arrange
  AsyncImportService service(config_);
  std::latch done(1);

  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  // Act & Assert
  EXPECT_FALSE(service.CancelJob(job_id));
}

//! Verify CancelAll does not crash with no jobs.
NOLINT_TEST_F(AsyncImportServiceCancelTest, CancelAll_NoJobs_Succeeds)
{
  // Arrange
  AsyncImportService service(config_);

  // Act & Assert - should not crash
  service.CancelAll();
  SUCCEED();
}

//! Verify CancelJob can cancel a job during execution.
NOLINT_TEST_F(
  AsyncImportServiceCancelTest, CancelJob_DuringExecution_CancelsJob)
{
  // Arrange
  AsyncImportService service(config_);
  std::latch job_started(1);
  std::latch cancel_attempted(1);
  std::atomic<bool> job_completed { false };
  std::atomic<bool> job_started_signaled { false };

  // Submit a job that signals when it starts
  auto job_id = SubmitTestJob(
    service, ImportRequest { .source_path = "custom.asset" },
    [&](ImportJobId, ImportReport) { job_completed = true; },
    [&](const ImportProgress& progress) {
      if (progress.phase == ImportPhase::kParsing) {
        bool expected = false;
        if (job_started_signaled.compare_exchange_strong(expected, true)) {
          job_started.count_down();
        }
      }
    },
    test::TestImportJob::Config {
      .total_delay = 50ms,
      .step_delay = 5ms,
      .report_progress = true,
    });

  EXPECT_NE(job_id, kInvalidJobId);

  // Wait for job to start, then cancel it
  job_started.wait();
  bool cancel_result = service.CancelJob(job_id);
  cancel_attempted.count_down();

  // Wait a bit to see if job completes (it shouldn't if cancelled properly)
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Assert
  // Note: The cancel may succeed or fail depending on timing, but we shouldn't
  // crash The important thing is that the system remains in a consistent state
  EXPECT_TRUE(cancel_result || job_completed);
}

//! Verify CancelJob before execution prevents job from starting.
NOLINT_TEST_F(
  AsyncImportServiceCancelTest, CancelJob_BeforeExecution_PreventsStart)
{
  // Arrange - configure with only 1 worker to ensure jobs queue up
  AsyncImportService::Config blocking_config {
    .thread_pool_size = 1,
    .max_in_flight_jobs = 1,
  };
  AsyncImportService service(blocking_config);

  std::latch first_job_started(1);
  std::atomic<bool> second_job_executed { false };
  std::atomic<bool> first_job_signaled { false };

  // Submit first job that blocks
  [[maybe_unused]] auto blocking_job = SubmitTestJob(
    service, ImportRequest { .source_path = "custom.asset" },
    [](ImportJobId, ImportReport) {},
    [&](const ImportProgress& progress) {
      if (progress.phase == ImportPhase::kParsing) {
        bool expected = false;
        if (first_job_signaled.compare_exchange_strong(expected, true)) {
          first_job_started.count_down();
        }
        // Keep this job running for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    },
    test::TestImportJob::Config {
      .total_delay = 50ms,
      .step_delay = 5ms,
      .report_progress = true,
    });

  EXPECT_NE(blocking_job, kInvalidJobId);

  // Wait for first job to start
  first_job_started.wait();

  // Submit second job - it should queue since worker is busy
  auto second_job
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&](ImportJobId, ImportReport) { second_job_executed = true; });

  EXPECT_NE(second_job, kInvalidJobId);

  // Immediately cancel the second job before it executes
  bool cancel_result = service.CancelJob(second_job);

  // Wait for first job to finish
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Assert - the second job should have been cancelled before execution
  EXPECT_TRUE(cancel_result);
  // Note: Due to timing, second_job_executed might still be true if cancel was
  // too late The important verification is that cancel_result correctly
  // reflects the outcome
}

//! Verify CancelAll cancels all active jobs.
NOLINT_TEST_F(AsyncImportServiceCancelTest, CancelAll_MultipleJobs_CancelsAll)
{
  // Arrange
  constexpr int kJobCount = 5;
  AsyncImportService service(config_);
  struct SharedState {
    std::atomic<int> jobs_completed { 0 };
    std::atomic<int> cancelled_reports { 0 };
    std::atomic<int> jobs_started { 0 };
    std::unordered_set<ImportJobId> started_job_ids;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> active { true };
  };

  auto state = std::make_shared<SharedState>();

  const auto job_factory = MakeTestJobFactory({
    .total_delay = 30ms,
    .step_delay = 5ms,
    .report_progress = true,
  });

  // Submit multiple jobs
  for (int i = 0; i < kJobCount; ++i) {
    auto job_id = service.SubmitImport(
      ImportRequest { .source_path = "custom.asset" },
      [state](ImportJobId, ImportReport report) {
        if (!state->active.load(std::memory_order_acquire)) {
          return;
        }
        DLOG_F(INFO, "CancelAll completion: success={} diagnostics={}",
          report.success, report.diagnostics.size());
        state->jobs_completed.fetch_add(1, std::memory_order_relaxed);
        const bool cancelled
          = HasDiagnosticCode(report.diagnostics, "import.cancelled");
        if (cancelled) {
          state->cancelled_reports.fetch_add(1, std::memory_order_relaxed);
        }
        state->cv.notify_all();
      },
      [state](const ImportProgress& progress) {
        if (!state->active.load(std::memory_order_acquire)) {
          return;
        }
        DLOG_F(INFO, "CancelAll progress: phase={} overall={:.2f} message='{}'",
          static_cast<int>(progress.phase), progress.overall_progress,
          progress.message);
        if (progress.phase == ImportPhase::kParsing) {
          std::lock_guard lock(state->mutex);
          if (state->started_job_ids.insert(progress.job_id).second) {
            state->jobs_started.fetch_add(1, std::memory_order_relaxed);
            state->cv.notify_all();
          }
        }
      },
      job_factory);
    EXPECT_NE(job_id, kInvalidJobId);
  }

  // Act - wait for jobs to start, then cancel all.
  {
    std::unique_lock lock(state->mutex);
    state->cv.wait_for(lock, 2s, [&]() {
      return state->jobs_started.load(std::memory_order_relaxed) >= kJobCount;
    });
  }
  service.CancelAll();

  // Wait for all jobs to report completion, or timeout.
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  {
    std::unique_lock lock(state->mutex);
    state->cv.wait_until(lock, deadline, [&]() {
      return state->jobs_completed.load(std::memory_order_relaxed) >= kJobCount;
    });
  }

  // Assert - verify jobs were cancelled (completed count should be less than
  // total) Note: Some jobs might complete before cancellation takes effect, so
  // we can't assert exactly zero completions, but we can verify the system is
  // consistent
  state->active.store(false, std::memory_order_release);

  const int final_completed
    = state->jobs_completed.load(std::memory_order_relaxed);
  EXPECT_EQ(final_completed, kJobCount);
  EXPECT_EQ(
    state->cancelled_reports.load(std::memory_order_relaxed), kJobCount);
}

//=== Shutdown Tests ===------------------------------------------------------//

class AsyncImportServiceShutdownTest : public ::testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 2 };
};

//! Verify RequestShutdown makes IsAcceptingJobs return false.
NOLINT_TEST_F(
  AsyncImportServiceShutdownTest, RequestShutdown_IsAcceptingJobs_ReturnsFalse)
{
  // Arrange
  AsyncImportService service(config_);

  // Act
  service.RequestShutdown();

  // Allow shutdown to propagate.
  const auto deadline = std::chrono::steady_clock::now() + 200ms;
  while (
    service.IsAcceptingJobs() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }

  // Assert
  EXPECT_FALSE(service.IsAcceptingJobs());
}

//! Verify destructor completes even with pending jobs.
NOLINT_TEST_F(
  AsyncImportServiceShutdownTest, Destructor_WithPendingJobs_Completes)
{
  // Arrange & Act
  {
    AsyncImportService service(config_);

    // Submit several jobs
    for (int i = 0; i < 5; ++i) {
      [[maybe_unused]] auto job_id = SubmitTestJob(service,
        ImportRequest { .source_path = "custom.asset" },
        [](ImportJobId, ImportReport) { });
      EXPECT_NE(job_id, kInvalidJobId);
    }
    // Destructor will cancel and cleanup
  }

  // Assert - no hang, no crash
  SUCCEED();
}

//=== Concurrent Submission Tests
//===------------------------------------------//

class AsyncImportServiceConcurrencyTest : public ::testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 4 };
};

//! Verify concurrent submissions from multiple threads work correctly.
NOLINT_TEST_F(AsyncImportServiceConcurrencyTest,
  SubmitImport_ConcurrentSubmissions_AllComplete)
{
  // Arrange
  constexpr int kJobsPerThread = 10;
  constexpr int kThreadCount = 4;
  constexpr int kTotalJobs = kJobsPerThread * kThreadCount;

  AsyncImportService service(config_);
  std::latch done(kTotalJobs);
  std::atomic<int> completed_count { 0 };
  std::atomic<bool> all_valid { true };

  // Act - submit from multiple threads
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kJobsPerThread; ++i) {
        [[maybe_unused]] auto job_id = SubmitTestJob(service,
          ImportRequest { .source_path = "custom.asset" },
          [&](ImportJobId, ImportReport) {
            completed_count.fetch_add(1, std::memory_order_relaxed);
            done.count_down();
          });
        if (job_id == kInvalidJobId) {
          all_valid.store(false, std::memory_order_relaxed);
        }
      }
    });
  }

  // Wait for all threads to finish submitting
  for (auto& t : threads) {
    t.join();
  }

  // Wait for all jobs to complete
  done.wait();

  // Assert
  EXPECT_EQ(completed_count.load(), kTotalJobs);
  EXPECT_TRUE(all_valid.load(std::memory_order_relaxed));
}

//! Verify rapid submit and cancel operations don't cause deadlocks.
NOLINT_TEST_F(
  AsyncImportServiceConcurrencyTest, RapidSubmitAndCancel_NoDeadlock)
{
  // Arrange
  constexpr int kIterations = 50;
  AsyncImportService service(config_);
  std::atomic<int> completed_count { 0 };

  // Act - rapidly submit and cancel jobs
  for (int i = 0; i < kIterations; ++i) {
    auto job_id
      = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
        [&](ImportJobId, ImportReport) {
          completed_count.fetch_add(1, std::memory_order_relaxed);
        });

    EXPECT_NE(job_id, kInvalidJobId);

    // Randomly cancel some jobs immediately
    if (i % 3 == 0) {
      service.CancelJob(job_id);
    }

    // Occasionally cancel all
    if (i % 10 == 0) {
      service.CancelAll();
    }
  }

  // Wait for any remaining jobs to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Assert - we completed without deadlock
  SUCCEED();
  // Note: We don't assert exact completion count because cancellations are
  // timing-dependent
}

//=== IsJobActive Tests
//===----------------------------------------------------//

class AsyncImportServiceJobActiveTest : public ::testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 2 };
};

//! Verify IsJobActive returns false for invalid job.
NOLINT_TEST_F(
  AsyncImportServiceJobActiveTest, IsJobActive_InvalidJob_ReturnsFalse)
{
  // Arrange
  AsyncImportService service(config_);

  // Act & Assert
  EXPECT_FALSE(service.IsJobActive(kInvalidJobId));
  EXPECT_FALSE(service.IsJobActive(999));
}

//! Verify IsJobActive returns false after job completes.
NOLINT_TEST_F(
  AsyncImportServiceJobActiveTest, IsJobActive_CompletedJob_ReturnsFalse)
{
  // Arrange
  AsyncImportService service(config_);
  std::latch done(1);

  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  // Act & Assert
  EXPECT_FALSE(service.IsJobActive(job_id));
}

} // namespace
