//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>
#include <vector>

#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;
using namespace oxygen::content::import;

namespace {

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
    = service.SubmitImport(ImportRequest { .source_path = "test.txt" },
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
    = service.SubmitImport(ImportRequest { .source_path = "test.txt" },
      [&](ImportJobId id, ImportReport) {
        callback_invoked = true;
        received_id = id;
        done.count_down();
      });

  done.wait();

  // Assert
  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(received_id, job_id);
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
  [[maybe_unused]] auto job_id = service.SubmitImport(
    ImportRequest { .source_path = "test.txt" },
    [&done](ImportJobId, ImportReport) { done.count_down(); },
    [&progress_invoked](const ImportProgress& progress) {
      if (progress.phase == ImportPhase::kParsing) {
        progress_invoked = true;
      }
    });

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
  auto id1 = service.SubmitImport(ImportRequest { .source_path = "file1.txt" },
    [&done](ImportJobId, ImportReport) { done.count_down(); });

  auto id2 = service.SubmitImport(ImportRequest { .source_path = "file2.txt" },
    [&done](ImportJobId, ImportReport) { done.count_down(); });

  auto id3 = service.SubmitImport(ImportRequest { .source_path = "file3.txt" },
    [&done](ImportJobId, ImportReport) { done.count_down(); });

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
    = service.SubmitImport(ImportRequest { .source_path = "test.txt" },
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
    = service.SubmitImport(ImportRequest { .source_path = "test.txt" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

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
      [[maybe_unused]] auto job_id = service.SubmitImport(
        ImportRequest { .source_path = "file" + std::to_string(i) + ".txt" },
        [](ImportJobId, ImportReport) { });
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

  // Act - submit from multiple threads
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kJobsPerThread; ++i) {
        [[maybe_unused]] auto job_id = service.SubmitImport(
          ImportRequest { .source_path = "thread" + std::to_string(t) + "_file"
              + std::to_string(i) + ".txt" },
          [&](ImportJobId, ImportReport) {
            completed_count.fetch_add(1, std::memory_order_relaxed);
            done.count_down();
          });
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
    = service.SubmitImport(ImportRequest { .source_path = "test.txt" },
      [&done](ImportJobId, ImportReport) { done.count_down(); });

  done.wait();

  // Act & Assert
  EXPECT_FALSE(service.IsJobActive(job_id));
}

} // namespace
