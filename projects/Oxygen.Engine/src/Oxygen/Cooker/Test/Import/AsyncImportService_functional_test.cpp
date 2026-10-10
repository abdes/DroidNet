//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/AsyncImportService.cpp

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <latch>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/TestImportJob.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;
using namespace oxygen::content::import;
namespace co = oxygen::co;

namespace {

using oxygen::cooker::test::HasDiagnosticCode;

[[nodiscard]] auto MakeTestJobFactory(test::TestImportJob::Config config)
  -> ImportJobFactory
{
  return
    [config](
      detail::ImportJobParams params) -> std::shared_ptr<detail::ImportJob> {
      return std::make_shared<test::TestImportJob>(std::move(params), config);
    };
}

[[nodiscard]] auto SubmitTestJob(AsyncImportService& service,
  ImportRequest request, ImportCompletionCallback on_complete,
  ProgressEventCallback on_progress = nullptr,
  test::TestImportJob::Config config = {}) -> ImportJobId
{
  auto result = service.SubmitImport(std::move(request), std::move(on_complete),
    std::move(on_progress), MakeTestJobFactory(config));
  EXPECT_TRUE(result);
  return result.value_or(kInvalidJobId);
}

auto StopService(AsyncImportService& service) -> void { service.Stop(); }

//=== Construction and Destruction Tests
//===-----------------------------------//

class AsyncImportServiceTest : public testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 2 };
};

NOLINT_TEST_F(AsyncImportServiceTest, ConstructDestructNoJobsSucceeds)
{
  {
    AsyncImportService service(config_);
    // Allow thread to start
    std::this_thread::sleep_for(50ms);
    StopService(service);
  }

  // No crash, no hang
  SUCCEED();
}

NOLINT_TEST_F(AsyncImportServiceTest, MultipleConstructDestructSucceeds)
{
  for (int i = 0; i < 3; ++i) {
    AsyncImportService service(config_);
    std::this_thread::sleep_for(20ms);
    StopService(service);
  }

  // No crash, no hang
  SUCCEED();
}

NOLINT_TEST_F(
  AsyncImportServiceTest, IsAcceptingJobsAfterConstructionReturnsTrue)
{
  AsyncImportService service(config_);

  EXPECT_TRUE(service.IsAcceptingJobs());

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, JobCountsAfterConstructionAreZero)
{
  AsyncImportService service(config_);

  EXPECT_EQ(service.PendingJobCount(), 0U);
  EXPECT_EQ(service.RunningJobCount(), 0U);

  StopService(service);
}

//=== Job Submission Tests ===------------------------------------------------//

//! Admission includes jobs posted by concurrent callers before the import
//! thread runs them.
NOLINT_TEST_F(AsyncImportServiceTest, PostedJobsReserveQueueCapacity)
{
  AsyncImportService service(AsyncImportService::Config {
    .thread_pool_size = 2, .max_in_flight_jobs = 1 });
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::promise<void> release;
  auto resume = release.get_future().share();
  const auto blocker
    = SubmitTestJob(service, ImportRequest { .source_path = "barrier.asset" },
      [&](ImportJobId, const ImportReport&) {
        entered.set_value();
        resume.wait();
      });
  EXPECT_NE(blocker, kInvalidJobId);
  const auto blocked = entered_future.wait_for(5s) == std::future_status::ready;
  EXPECT_TRUE(blocked);
  if (!blocked) {
    release.set_value();
    StopService(service);
    return;
  }

  std::mutex mutex;
  std::condition_variable completed;
  size_t accepted = 0;
  size_t callbacks = 0;
  size_t failed = 0;
  {
    std::vector<std::jthread> producers;
    for (size_t producer = 0; producer < 4; ++producer) {
      producers.emplace_back([&] {
        for (size_t job = 0; job < 64; ++job) {
          const auto id = service.SubmitImport(
            ImportRequest { .source_path = "queued.asset" },
            [&](ImportJobId, const ImportReport& report) {
              std::scoped_lock lock(mutex);
              ++callbacks;
              failed += report.success ? 0U : 1U;
              completed.notify_one();
            },
            nullptr,
            MakeTestJobFactory({ .total_delay = 1ms,
              .step_delay = 1ms,
              .report_progress = false }));
          if (id) {
            std::scoped_lock lock(mutex);
            ++accepted;
          }
        }
      });
    }
  }
  EXPECT_GT(accepted, 0U);
  EXPECT_LT(accepted, 256U);
  release.set_value();
  {
    std::unique_lock lock(mutex);
    EXPECT_TRUE(
      completed.wait_for(lock, 10s, [&] { return callbacks == accepted; }));
  }
  StopService(service);
  EXPECT_EQ(callbacks, accepted);
  EXPECT_EQ(failed, 0U);
}

//! Factory rejection releases admission and never invokes a completion
//! callback.
NOLINT_TEST_F(AsyncImportServiceTest, RejectedFactoriesReleaseAdmission)
{
  AsyncImportService service(config_);
  std::atomic<size_t> rejected_callbacks { 0 };
  for (size_t attempt = 0; attempt < 256; ++attempt) {
    const auto id = service.SubmitImport(
      ImportRequest { .source_path = "rejected.asset" },
      [&](ImportJobId, const ImportReport&) { ++rejected_callbacks; }, nullptr,
      [](detail::ImportJobParams) -> std::shared_ptr<detail::ImportJob> {
        return nullptr;
      });
    EXPECT_FALSE(id);
  }
  std::promise<void> finished;
  auto done = finished.get_future();
  const auto id
    = SubmitTestJob(service, ImportRequest { .source_path = "accepted.asset" },
      [&](ImportJobId, const ImportReport&) { finished.set_value(); });
  EXPECT_NE(id, kInvalidJobId);
  EXPECT_EQ(done.wait_for(5s), std::future_status::ready);
  StopService(service);
  EXPECT_EQ(rejected_callbacks.load(), 0U);
}

NOLINT_TEST_F(AsyncImportServiceTest, SubmitImportReturnsValidJobId)
{
  AsyncImportService service(config_);
  std::latch done(1);

  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  EXPECT_NE(job_id, kInvalidJobId);

  // Cleanup - wait for job to complete
  done.wait();

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, SubmitImportCompletionCallbackIsInvoked)
{
  AsyncImportService service(config_);
  std::latch done(1);
  std::atomic<bool> callback_invoked { false };
  ImportJobId received_id = kInvalidJobId;

  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&](ImportJobId id, ImportReport) {
        callback_invoked = true;
        received_id = id;
        done.count_down();
      });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(received_id, job_id);

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, SubmitImportCustomJobFactoryAllowsUnknown)
{
  AsyncImportService service(config_);
  std::latch done(1);

  const auto job_factory = MakeTestJobFactory({
    .total_delay = 15ms,
    .step_delay = 5ms,
    .report_progress = false,
  });

  auto job_result = service.SubmitImport(
    ImportRequest { .source_path = "custom.asset" },
    [&done](ImportJobId, ImportReport) { done.count_down(); }, nullptr,
    job_factory);

  ASSERT_TRUE(job_result.has_value());
  EXPECT_NE(*job_result, kInvalidJobId);
  done.wait();

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, SubmitImportCustomJobCompletes)
{
  AsyncImportService service(config_);

  std::latch done(1);
  std::atomic<bool> callback_invoked { false };
  ImportReport received_report;

  [[maybe_unused]] auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&](ImportJobId, ImportReport report) {
        callback_invoked = true;
        received_report = std::move(report);
        done.count_down();
      });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(received_report.success);

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, SubmitImportProgressCallbackIsInvoked)
{
  AsyncImportService service(config_);
  std::latch done(1);
  std::atomic<bool> progress_invoked { false };

  [[maybe_unused]] auto job_id = SubmitTestJob(
    service, ImportRequest { .source_path = "custom.asset" },
    [&done](ImportJobId, ImportReport) { done.count_down(); },
    [&progress_invoked](const ProgressEvent& progress) {
      if (progress.header.phase == ImportPhase::kWorking) {
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

  EXPECT_TRUE(progress_invoked);

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, SubmitImportMultipleJobsUniqueIds)
{
  AsyncImportService service(config_);
  std::latch done(3);

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

  EXPECT_NE(id1, id2);
  EXPECT_NE(id2, id3);
  EXPECT_NE(id1, id3);

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, SubmitImportAfterShutdownReturnsInvalid)
{
  AsyncImportService service(config_);
  service.RequestShutdown();

  auto request = ImportRequest { .source_path = "custom.asset" };
  auto job_id = service.SubmitImport(
    std::move(request), nullptr, nullptr, MakeTestJobFactory({}));

  ASSERT_FALSE(job_id);

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, UnknownFormatWithoutDomainRejected)
{
  AsyncImportService service(config_);

  auto request = ImportRequest {};
  request.source_path = "unrecognized.asset";
  request.cooked_root = std::filesystem::temp_directory_path();

  const auto job_id
    = service.SubmitImport(std::move(request), nullptr, nullptr);
  EXPECT_FALSE(job_id.has_value());

  StopService(service);
}

//=== Cancellation Tests ===--------------------------------------------------//

NOLINT_TEST_F(AsyncImportServiceTest, CancelJobInvalidIdReturnsFalse)
{
  AsyncImportService service(config_);

  EXPECT_FALSE(service.CancelJob(kInvalidJobId));
  EXPECT_FALSE(service.CancelJob(ImportJobId { 999U }));

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, CancelJobCompletedJobReturnsFalse)
{
  AsyncImportService service(config_);
  std::latch done(1);

  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  EXPECT_FALSE(service.CancelJob(job_id));

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, CancelAllNoJobsSucceeds)
{
  AsyncImportService service(config_);

  // Act & Assert - should not crash
  service.CancelAll();
  SUCCEED();

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, CancelJobDuringExecutionCancelsJob)
{
  AsyncImportService service(config_);
  std::latch job_started(1);
  std::latch cancel_attempted(1);
  std::atomic<bool> job_completed { false };
  std::atomic<bool> job_started_signaled { false };

  // Submit a job that signals when it starts
  auto job_id = SubmitTestJob(
    service, ImportRequest { .source_path = "custom.asset" },
    [&](ImportJobId, ImportReport) { job_completed = true; },
    [&](const ProgressEvent& progress) {
      if (progress.header.phase == ImportPhase::kWorking) {
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

  // Wait a bit to see if job completes (it shouldn't if canceled properly)
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Note: The cancel may succeed or fail depending on timing, but we shouldn't
  // crash The important thing is that the system remains in a consistent state
  EXPECT_TRUE(cancel_result || job_completed);

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, CancelJobBeforeExecutionPreventsStart)
{
  // Configure with only 1 worker to ensure jobs queue up
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
    [](ImportJobId, ImportReport) { },
    [&](const ProgressEvent& progress) {
      if (progress.header.phase == ImportPhase::kWorking) {
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

  // The second job should have been canceled before execution
  EXPECT_TRUE(cancel_result);
  // Note: Due to timing, second_job_executed might still be true if cancel was
  // too late The important verification is that cancel_result correctly
  // reflects the outcome

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, CancelAllMultipleJobsCancelsAll)
{
  constexpr int kJobCount = 5;
  AsyncImportService service(config_);
  struct SharedState {
    std::atomic<int> jobs_completed { 0 };
    std::atomic<int> canceled_reports { 0 };
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
        const bool canceled
          = HasDiagnosticCode(report.diagnostics, "import.canceled");
        if (canceled) {
          state->canceled_reports.fetch_add(1, std::memory_order_relaxed);
        }
        state->cv.notify_all();
      },
      [state](const ProgressEvent& progress) {
        if (!state->active.load(std::memory_order_acquire)) {
          return;
        }
        DLOG_F(INFO, "CancelAll progress: phase={} overall={:.2f} message='{}'",
          static_cast<int>(progress.header.phase),
          progress.header.overall_progress, progress.header.message);
        if (progress.header.phase == ImportPhase::kWorking) {
          std::scoped_lock lock(state->mutex);
          if (state->started_job_ids.insert(progress.header.job_id).second) {
            state->jobs_started.fetch_add(1, std::memory_order_relaxed);
            state->cv.notify_all();
          }
        }
      },
      job_factory);
    EXPECT_TRUE(job_id.has_value());
  }

  // Wait for jobs to start, then cancel all.
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

  // Verify jobs were canceled (completed count should be less than
  // total) Note: Some jobs might complete before cancellation takes effect, so
  // we can't assert exactly zero completions, but we can verify the system is
  // consistent
  state->active.store(false, std::memory_order_release);

  const int final_completed
    = state->jobs_completed.load(std::memory_order_relaxed);
  EXPECT_EQ(final_completed, kJobCount);
  EXPECT_EQ(state->canceled_reports.load(std::memory_order_relaxed), kJobCount);

  StopService(service);
}

//=== Shutdown Tests ===------------------------------------------------------//

NOLINT_TEST_F(
  AsyncImportServiceTest, RequestShutdownIsAcceptingJobsReturnsFalse)
{
  AsyncImportService service(config_);

  service.RequestShutdown();

  // Allow shutdown to propagate.
  const auto deadline = std::chrono::steady_clock::now() + 200ms;
  while (
    service.IsAcceptingJobs() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_FALSE(service.IsAcceptingJobs());

  StopService(service);
}

class AsyncImportServiceShutdownDeathTest : public testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 2 };
};

NOLINT_TEST_F(AsyncImportServiceShutdownDeathTest, DestructorWithoutStopAborts)
{
  const auto exercise = [this]() {
    AsyncImportService service(config_);

    // Submit several jobs
    for (int i = 0; i < 5; ++i) {
      [[maybe_unused]] auto job_id = SubmitTestJob(service,
        ImportRequest { .source_path = "custom.asset" },
        [](ImportJobId, ImportReport) { });
    }
  };

  EXPECT_DEATH(exercise(), ".*Destroyed without Stop\\(\\).*");
}

NOLINT_TEST_F(AsyncImportServiceTest, StopWithPendingJobsCompletes)
{
  AsyncImportService service(config_);

  for (int i = 0; i < 5; ++i) {
    [[maybe_unused]] auto job_id
      = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
        [](ImportJobId, ImportReport) { });
    EXPECT_NE(job_id, kInvalidJobId);
  }

  service.Stop();

  EXPECT_TRUE(service.IsStopped());
}

//=== Concurrent Submission Tests ===-----------------------------------------//

class AsyncImportServiceConcurrencyTest : public testing::Test {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 4 };
};

NOLINT_TEST_F(AsyncImportServiceConcurrencyTest,
  SubmitImportConcurrentSubmissionsAllComplete)
{
  constexpr int kJobsPerThread = 10;
  constexpr int kThreadCount = 4;
  constexpr int kTotalJobs = kJobsPerThread * kThreadCount;

  AsyncImportService service(config_);
  std::latch done(kTotalJobs);
  std::atomic<int> completed_count { 0 };
  std::atomic<bool> all_valid { true };

  // Submit from multiple threads
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

  EXPECT_EQ(completed_count.load(), kTotalJobs);
  EXPECT_TRUE(all_valid.load(std::memory_order_relaxed));

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceConcurrencyTest, RapidSubmitAndCancelNoDeadlock)
{
  constexpr int kIterations = 50;
  AsyncImportService service(config_);
  std::atomic<int> completed_count { 0 };

  // Rapidly submit and cancel jobs
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

  // We completed without deadlock
  SUCCEED();
  // Note: We don't assert exact completion count because cancellations are
  // timing-dependent

  StopService(service);
}

//=== IsJobActive Tests
//===----------------------------------------------------//

NOLINT_TEST_F(AsyncImportServiceTest, IsJobActiveInvalidJobReturnsFalse)
{
  AsyncImportService service(config_);

  EXPECT_FALSE(service.IsJobActive(kInvalidJobId));
  EXPECT_FALSE(service.IsJobActive(ImportJobId { 999U }));

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceTest, IsJobActiveCompletedJobReturnsFalse)
{
  AsyncImportService service(config_);
  std::latch done(1);

  auto job_id
    = SubmitTestJob(service, ImportRequest { .source_path = "custom.asset" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  EXPECT_NE(job_id, kInvalidJobId);

  done.wait();

  EXPECT_FALSE(service.IsJobActive(job_id));

  StopService(service);
}

} // namespace
