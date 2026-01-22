//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/IAsyncFileReader.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Internal/AsyncImporter.h>
#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/ImportJob.h>
#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>

using namespace std::chrono_literals;
using namespace oxygen::content::import;
using namespace oxygen::content::import::detail;
using oxygen::co::Co;
using oxygen::co::Event;
using oxygen::co::kJoin;
using oxygen::co::kYield;

namespace {

[[nodiscard]] auto MakeSuccessReport(const ImportRequest& request)
  -> ImportReport
{
  ImportReport report {
    .cooked_root
    = request.cooked_root.value_or(request.source_path.parent_path()),
    .success = true,
  };
  return report;
}

class TestImportJob final : public ImportJob {
public:
  OXYGEN_TYPED(TestImportJob)

  using ImportJob::ImportJob;

private:
  [[nodiscard]] auto ExecuteAsync() -> Co<ImportReport> override
  {
    ReportPhaseProgress(ImportPhase::kWorking, 0.1f, "Test job running");
    co_return MakeSuccessReport(Request());
  }
};

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
  std::unique_ptr<IAsyncFileReader> file_reader_;
  std::unique_ptr<IAsyncFileWriter> file_writer_;
  std::unique_ptr<oxygen::co::ThreadPool> thread_pool_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;
  AsyncImporter::Config config_ { .channel_capacity = 8 };

  [[nodiscard]] auto MakeTestCookedRoot() const -> std::filesystem::path
  {
    const auto* test_info
      = ::testing::UnitTest::GetInstance()->current_test_info();
    DCHECK_F(test_info != nullptr);

    auto name = std::string(test_info->test_suite_name()) + "_"
      + std::string(test_info->name());

    return std::filesystem::temp_directory_path() / "oxygen_async_import_tests"
      / std::move(name) / ".cooked";
  }

  void SetUp() override
  {
    file_reader_ = CreateAsyncFileReader(loop_);
    file_writer_ = CreateAsyncFileWriter(loop_);
    table_registry_ = std::make_unique<ResourceTableRegistry>(*file_writer_);
    thread_pool_ = std::make_unique<oxygen::co::ThreadPool>(loop_, 1);
    config_.file_writer = file_writer_.get();
    config_.table_registry = table_registry_.get();
  }

  [[nodiscard]] auto MakeJob(ImportJobId job_id, ImportRequest request,
    ImportCompletionCallback on_complete, ProgressEventCallback on_progress,
    std::shared_ptr<Event> cancel_event) -> std::shared_ptr<TestImportJob>
  {
    return std::make_shared<TestImportJob>(job_id, std::move(request),
      std::move(on_complete), std::move(on_progress), std::move(cancel_event),
      oxygen::observer_ptr<IAsyncFileReader>(file_reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(file_writer_.get()),
      oxygen::observer_ptr<oxygen::co::ThreadPool>(thread_pool_.get()),
      oxygen::observer_ptr<ResourceTableRegistry>(table_registry_.get()),
      ImportConcurrency {});
  }

  void TearDown() override
  {
    {
      std::error_code ec;
      std::filesystem::remove_all(MakeTestCookedRoot(), ec);
    }

    {
      std::error_code ec;
      std::filesystem::remove_all(
        std::filesystem::current_path() / ".cooked", ec);
    }
  }
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

      ImportRequest request;
      request.source_path = "test.txt";
      request.cooked_root = MakeTestCookedRoot();

      auto cancel_event = std::make_shared<Event>();
      auto on_complete = [&](ImportJobId id, const ImportReport& report) {
        received_id = id;
        received_success = report.success;
        callback_called = true;
        completion_event.Trigger();
      };

      auto job = MakeJob(ImportJobId { 42U }, std::move(request),
        std::move(on_complete), nullptr, cancel_event);

      JobEntry entry;
      entry.job_id = ImportJobId { 42U };
      entry.job = std::move(job);
      entry.cancel_event = cancel_event;

      co_await importer.SubmitJob(std::move(entry));

      // Wait for completion
      co_await completion_event;

      importer.Stop();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(received_id, ImportJobId { 42U });
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

      for (uint64_t i = 1; i <= 3; ++i) {
        const ImportJobId job_id { i };
        ImportRequest request;
        request.source_path = "test" + std::to_string(i) + ".txt";
        request.cooked_root = MakeTestCookedRoot();

        auto cancel_event = std::make_shared<Event>();
        auto on_complete = [&](ImportJobId id, const ImportReport&) {
          {
            std::lock_guard lock(order_mutex);
            completion_order.push_back(id);
          }
          if (++completed_count == 3) {
            all_done.Trigger();
          }
        };

        auto job = MakeJob(job_id, std::move(request), std::move(on_complete),
          nullptr, cancel_event);

        JobEntry entry;
        entry.job_id = job_id;
        entry.job = std::move(job);
        entry.cancel_event = cancel_event;

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
  EXPECT_THAT(completion_order,
    ElementsAre(ImportJobId { 1U }, ImportJobId { 2U }, ImportJobId { 3U }));
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

      ImportRequest request;
      request.source_path = "test.txt";
      request.cooked_root = MakeTestCookedRoot();

      auto cancel_event = std::make_shared<Event>();
      auto on_progress = [&](const ProgressEvent& progress) {
        progress_job_id = progress.header.job_id;
        progress_called = true;
      };

      auto on_complete
        = [&](ImportJobId, const ImportReport&) { completion_event.Trigger(); };

      auto job = MakeJob(ImportJobId { 99U }, std::move(request),
        std::move(on_complete), std::move(on_progress), cancel_event);

      JobEntry entry;
      entry.job_id = ImportJobId { 99U };
      entry.job = std::move(job);
      entry.cancel_event = cancel_event;

      co_await importer.SubmitJob(std::move(entry));

      // Wait for completion
      co_await completion_event;

      importer.Stop();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(progress_called);
  EXPECT_EQ(progress_job_id, ImportJobId { 99U });
}

//=== Cancellation Tests ===-------------------------------------------------//

class AsyncImporterCancellationTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
  AsyncImporter::Config config_ { .channel_capacity = 8 };
  std::unique_ptr<IAsyncFileReader> file_reader_;
  std::unique_ptr<IAsyncFileWriter> file_writer_;
  std::unique_ptr<oxygen::co::ThreadPool> thread_pool_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;

  void SetUp() override
  {
    file_reader_ = CreateAsyncFileReader(loop_);
    file_writer_ = CreateAsyncFileWriter(loop_);
    table_registry_ = std::make_unique<ResourceTableRegistry>(*file_writer_);
    thread_pool_ = std::make_unique<oxygen::co::ThreadPool>(loop_, 1);
    config_.file_writer = file_writer_.get();
    config_.table_registry = table_registry_.get();
  }
};

//! Verify job with triggered cancel event calls cancellation callback.
NOLINT_TEST_F(
  AsyncImporterCancellationTest, CancelEvent_CompletesWithCancelledDiagnostic)
{
  // Arrange
  AsyncImporter importer(config_);
  std::atomic<bool> complete_called { false };
  ImportJobId completed_id = kInvalidJobId;
  bool received_success = true;
  std::string canceled_code;
  Event done_event;

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      importer.Run();

      auto cancel_event = std::make_shared<Event>();

      ImportRequest request;
      request.source_path = "test.txt";

      auto on_complete = [&](ImportJobId id, const ImportReport& report) {
        completed_id = id;
        received_success = report.success;
        if (!report.diagnostics.empty()) {
          canceled_code = report.diagnostics.front().code;
        }
        complete_called = true;
        done_event.Trigger();
      };

      auto job = std::make_shared<TestImportJob>(ImportJobId { 123U },
        std::move(request), std::move(on_complete), nullptr, cancel_event,
        oxygen::observer_ptr<IAsyncFileReader>(file_reader_.get()),
        oxygen::observer_ptr<IAsyncFileWriter>(file_writer_.get()),
        oxygen::observer_ptr<oxygen::co::ThreadPool>(thread_pool_.get()),
        oxygen::observer_ptr<ResourceTableRegistry>(table_registry_.get()),
        ImportConcurrency {});

      JobEntry entry;
      entry.job_id = ImportJobId { 123U };
      entry.job = std::move(job);
      entry.cancel_event = cancel_event;

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
  EXPECT_TRUE(complete_called);
  EXPECT_EQ(completed_id, ImportJobId { 123U });
  EXPECT_FALSE(received_success);
  EXPECT_EQ(canceled_code, "import.canceled");
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
  entry.job_id = ImportJobId { 1U };
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
  entry.job_id = ImportJobId { 1U };
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
  for (uint64_t i = 0; i < 2; ++i) {
    JobEntry entry;
    entry.job_id = ImportJobId { i };
    EXPECT_TRUE(importer.TrySubmitJob(std::move(entry)));
  }

  // Act
  JobEntry extra_entry;
  extra_entry.job_id = ImportJobId { 99U };
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
  entry.job_id = ImportJobId { 1U };
  const bool result = importer.TrySubmitJob(std::move(entry));

  // Assert
  EXPECT_FALSE(result);
}

} // namespace
