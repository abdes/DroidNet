//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <mutex>
#include <vector>

#include <Oxygen/Content/Import/Async/Detail/AsyncImporter.h>
#include <Oxygen/Content/Import/Async/ImportEventLoop.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;
using namespace oxygen::content::import;
using namespace oxygen::content::import::detail;
using oxygen::co::Co;
using oxygen::co::Event;
using oxygen::co::kJoin;
using oxygen::co::kYield;

namespace {

//=== Lifecycle Tests
//===---------------------------------------------------------//

class AsyncImporterLifecycleTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
  AsyncImporter::Config config_ { .channel_capacity = 8 };
};

//! Verify importer constructs and destructs without crash.
NOLINT_TEST_F(AsyncImporterLifecycleTest, ConstructDestruct_Succeeds)
{
  // Arrange & Act
  {
    AsyncImporter importer(config_);
  }

  // Assert - no crash
  SUCCEED();
}

//! Verify IsRunning returns false before activation.
NOLINT_TEST_F(
  AsyncImporterLifecycleTest, IsRunning_BeforeActivation_ReturnsFalse)
{
  // Arrange
  AsyncImporter importer(config_);

  // Act & Assert
  EXPECT_FALSE(importer.IsRunning());
}

//! Verify IsAcceptingJobs returns true after construction.
NOLINT_TEST_F(
  AsyncImporterLifecycleTest, IsAcceptingJobs_AfterConstruction_ReturnsTrue)
{
  // Arrange
  AsyncImporter importer(config_);

  // Act & Assert
  EXPECT_TRUE(importer.IsAcceptingJobs());
}

//! Verify full lifecycle: activate, run, stop.
NOLINT_TEST_F(
  AsyncImporterLifecycleTest, ActivateRunStop_FullLifecycle_Succeeds)
{
  // Arrange
  AsyncImporter importer(config_);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      // Activate the importer
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      EXPECT_TRUE(importer.IsRunning());

      // Start the processing loop
      importer.Run();

      // Stop the importer
      importer.Stop();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(importer.IsRunning());
}

//! Verify Stop closes the job channel.
NOLINT_TEST_F(AsyncImporterLifecycleTest, Stop_ClosesJobChannel)
{
  // Arrange
  AsyncImporter importer(config_);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      importer.Run();

      EXPECT_TRUE(importer.IsAcceptingJobs());
      importer.Stop();
      EXPECT_FALSE(importer.IsAcceptingJobs());

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(importer.IsAcceptingJobs());
}

//=== Job Submission Tests
//===-------------------------------------------------//

class AsyncImporterJobTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
  AsyncImporter::Config config_ { .channel_capacity = 8 };
};

//! Verify job submission and completion callback.
NOLINT_TEST_F(AsyncImporterJobTest, SubmitJob_CallsCompletionCallback)
{
  // Arrange
  AsyncImporter importer(config_);
  std::atomic<bool> callback_called { false };
  ImportJobId received_id = kInvalidJobId;
  bool received_success = false;
  Event completion_event;

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      importer.Run();

      JobEntry entry;
      entry.job_id = 42;
      entry.request.source_path = "test.txt";
      entry.on_complete = [&](ImportJobId id, const ImportReport& report) {
        received_id = id;
        received_success = report.success;
        callback_called = true;
        completion_event.Trigger();
      };

      co_await importer.SubmitJob(std::move(entry));

      // Wait for completion
      co_await completion_event;

      importer.Stop();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(received_id, 42U);
  EXPECT_TRUE(received_success);
}

//! Verify multiple jobs are processed in order.
NOLINT_TEST_F(AsyncImporterJobTest, SubmitMultipleJobs_ProcessedInOrder)
{
  // Arrange
  AsyncImporter importer(config_);
  std::vector<ImportJobId> completion_order;
  std::mutex order_mutex;
  std::atomic<int> completed_count { 0 };
  Event all_done;

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      importer.Run();

      for (ImportJobId i = 1; i <= 3; ++i) {
        JobEntry entry;
        entry.job_id = i;
        entry.request.source_path = "test" + std::to_string(i) + ".txt";
        entry.on_complete = [&](ImportJobId id, const ImportReport&) {
          {
            std::lock_guard lock(order_mutex);
            completion_order.push_back(id);
          }
          if (++completed_count == 3) {
            all_done.Trigger();
          }
        };

        co_await importer.SubmitJob(std::move(entry));
      }

      // Wait for all to complete
      co_await all_done;

      importer.Stop();
      co_return kJoin;
    };
  });

  // Assert
  using ::testing::ElementsAre;
  EXPECT_THAT(completion_order, ElementsAre(1, 2, 3));
}

//! Verify progress callback is invoked.
NOLINT_TEST_F(AsyncImporterJobTest, SubmitJob_CallsProgressCallback)
{
  // Arrange
  AsyncImporter importer(config_);
  std::atomic<bool> progress_called { false };
  ImportJobId progress_job_id = kInvalidJobId;
  Event completion_event;

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      importer.Run();

      JobEntry entry;
      entry.job_id = 99;
      entry.request.source_path = "test.txt";
      entry.on_progress = [&](const ImportProgress& progress) {
        progress_job_id = progress.job_id;
        progress_called = true;
      };
      entry.on_complete
        = [&](ImportJobId, const ImportReport&) { completion_event.Trigger(); };

      co_await importer.SubmitJob(std::move(entry));

      // Wait for completion
      co_await completion_event;

      importer.Stop();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(progress_called);
  EXPECT_EQ(progress_job_id, 99U);
}

//=== Cancellation Tests ===-------------------------------------------------//

class AsyncImporterCancellationTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
  AsyncImporter::Config config_ { .channel_capacity = 8 };
};

//! Verify job with triggered cancel event calls cancellation callback.
NOLINT_TEST_F(
  AsyncImporterCancellationTest, CancelEvent_CallsCancellationCallback)
{
  // Arrange
  AsyncImporter importer(config_);
  std::atomic<bool> cancel_called { false };
  std::atomic<bool> complete_called { false };
  ImportJobId cancelled_id = kInvalidJobId;
  Event done_event;

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      importer.Run();

      auto cancel_event = std::make_shared<Event>();

      JobEntry entry;
      entry.job_id = 123;
      entry.request.source_path = "test.txt";
      entry.cancel_event = cancel_event;
      entry.on_cancel = [&](ImportJobId id) {
        cancelled_id = id;
        cancel_called = true;
        done_event.Trigger();
      };
      entry.on_complete = [&](ImportJobId, const ImportReport&) {
        complete_called = true;
        done_event.Trigger();
      };

      // Trigger cancellation before processing
      cancel_event->Trigger();

      co_await importer.SubmitJob(std::move(entry));

      // Wait for done
      co_await done_event;

      importer.Stop();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(cancel_called);
  EXPECT_FALSE(complete_called);
  EXPECT_EQ(cancelled_id, 123U);
}

//! Verify CloseJobChannel prevents new submissions.
NOLINT_TEST_F(
  AsyncImporterCancellationTest, CloseJobChannel_PreventsSubmissions)
{
  // Arrange
  AsyncImporter importer(config_);

  // Act
  importer.CloseJobChannel();

  // Assert
  EXPECT_FALSE(importer.IsAcceptingJobs());

  JobEntry entry;
  entry.job_id = 1;
  EXPECT_FALSE(importer.TrySubmitJob(std::move(entry)));
}

//=== TrySubmitJob Tests ===-------------------------------------------------//

class AsyncImporterTrySubmitTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify TrySubmitJob succeeds when channel has space.
NOLINT_TEST_F(AsyncImporterTrySubmitTest, TrySubmitJob_WhenSpace_ReturnsTrue)
{
  // Arrange
  AsyncImporter importer({ .channel_capacity = 4 });

  // Act
  JobEntry entry;
  entry.job_id = 1;
  entry.request.source_path = "test.txt";
  const bool result = importer.TrySubmitJob(std::move(entry));

  // Assert
  EXPECT_TRUE(result);
}

//! Verify TrySubmitJob fails when channel is full.
NOLINT_TEST_F(AsyncImporterTrySubmitTest, TrySubmitJob_WhenFull_ReturnsFalse)
{
  // Arrange
  AsyncImporter importer({ .channel_capacity = 2 });

  // Fill the channel
  for (int i = 0; i < 2; ++i) {
    JobEntry entry;
    entry.job_id = static_cast<ImportJobId>(i);
    EXPECT_TRUE(importer.TrySubmitJob(std::move(entry)));
  }

  // Act
  JobEntry extra_entry;
  extra_entry.job_id = 99;
  const bool result = importer.TrySubmitJob(std::move(extra_entry));

  // Assert
  EXPECT_FALSE(result);
}

//! Verify TrySubmitJob fails when channel is closed.
NOLINT_TEST_F(AsyncImporterTrySubmitTest, TrySubmitJob_WhenClosed_ReturnsFalse)
{
  // Arrange
  AsyncImporter importer({ .channel_capacity = 4 });
  importer.CloseJobChannel();

  // Act
  JobEntry entry;
  entry.job_id = 1;
  const bool result = importer.TrySubmitJob(std::move(entry));

  // Assert
  EXPECT_FALSE(result);
}

} // namespace
