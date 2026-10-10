//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/AsyncImporter.cpp, Import/Internal/ImportJob.cpp

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/AsyncImporter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>
#include <Oxygen/Cooker/Import/Internal/ImportJobParams.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

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
  auto report = ImportReport {};
  report.cooked_root
    = request.cooked_root.value_or(request.source_path.parent_path());
  report.success = true;
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

//=== Job Submission Tests
//===-------------------------------------------------//

class AsyncImporterJobTest : public ::testing::Test {
protected:
  oxygen::cooker::test::ScopedTempDir cooked_dir_;
  ImportEventLoop loop_;
  std::unique_ptr<IAsyncFileReader> file_reader_;
  std::unique_ptr<IAsyncFileWriter> file_writer_;
  std::unique_ptr<oxygen::co::ThreadPool> thread_pool_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;
  AsyncImporter::Config config_ { .channel_capacity = 8 };

  [[nodiscard]] auto MakeTestCookedRoot() const -> std::filesystem::path
  {
    return cooked_dir_.Path() / ".cooked";
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
    return std::make_shared<TestImportJob>(ImportJobParams {
      .id = job_id,
      .request = std::move(request),
      .on_complete = std::move(on_complete),
      .on_progress = std::move(on_progress),
      .cancel_event = std::move(cancel_event),
      .reader = oxygen::observer_ptr<IAsyncFileReader>(file_reader_.get()),
      .writer = oxygen::observer_ptr<IAsyncFileWriter>(file_writer_.get()),
      .thread_pool
      = oxygen::observer_ptr<oxygen::co::ThreadPool>(thread_pool_.get()),
      .registry
      = oxygen::observer_ptr<ResourceTableRegistry>(table_registry_.get()),
      .concurrency = ImportConcurrency {},
      .stop_token = {},
    });
  }
};

NOLINT_TEST_F(AsyncImporterJobTest, SubmitJobCallsCompletionCallback)
{
  AsyncImporter importer(config_);
  std::atomic<bool> callback_called { false };
  ImportJobId received_id = kInvalidJobId;
  bool received_success = false;
  Event completion_event;

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

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(received_id, ImportJobId { 42U });
  EXPECT_TRUE(received_success);
}

NOLINT_TEST_F(AsyncImporterJobTest, SubmitMultipleJobsProcessedInOrder)
{
  AsyncImporter importer(config_);
  std::vector<ImportJobId> completion_order;
  std::mutex order_mutex;
  std::atomic<int> completed_count { 0 };
  Event all_done;

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
            std::scoped_lock lock(order_mutex);
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

  using ::testing::ElementsAre;
  EXPECT_THAT(completion_order,
    ElementsAre(ImportJobId { 1U }, ImportJobId { 2U }, ImportJobId { 3U }));
}

NOLINT_TEST_F(AsyncImporterJobTest, SubmitJobCallsProgressCallback)
{
  AsyncImporter importer(config_);
  std::atomic<bool> progress_called { false };
  ImportJobId progress_job_id = kInvalidJobId;
  Event completion_event;

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

  EXPECT_TRUE(progress_called);
  EXPECT_EQ(progress_job_id, ImportJobId { 99U });
}

//=== Cancellation Tests ===-------------------------------------------------//

class AsyncImporterCancellationTest : public ::testing::Test {
protected:
  oxygen::cooker::test::ScopedTempDir cooked_dir_;
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

NOLINT_TEST_F(
  AsyncImporterCancellationTest, CancelEventCompletesWithCancelledDiagnostic)
{
  AsyncImporter importer(config_);
  std::atomic<bool> complete_called { false };
  ImportJobId completed_id = kInvalidJobId;
  bool received_success = true;
  std::string canceled_code;
  Event done_event;

  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      importer.Run();

      auto cancel_event = std::make_shared<Event>();

      ImportRequest request;
      request.source_path = "test.txt";
      request.cooked_root = cooked_dir_.Path() / ".cooked";

      auto on_complete = [&](ImportJobId id, const ImportReport& report) {
        completed_id = id;
        received_success = report.success;
        if (!report.diagnostics.empty()) {
          canceled_code = report.diagnostics.front().code;
        }
        complete_called = true;
        done_event.Trigger();
      };

      auto job = std::make_shared<TestImportJob>(ImportJobParams {
        .id = ImportJobId { 123U },
        .request = std::move(request),
        .on_complete = std::move(on_complete),
        .on_progress = nullptr,
        .cancel_event = cancel_event,
        .reader = oxygen::observer_ptr<IAsyncFileReader>(file_reader_.get()),
        .writer = oxygen::observer_ptr<IAsyncFileWriter>(file_writer_.get()),
        .thread_pool
        = oxygen::observer_ptr<oxygen::co::ThreadPool>(thread_pool_.get()),
        .registry
        = oxygen::observer_ptr<ResourceTableRegistry>(table_registry_.get()),
        .concurrency = ImportConcurrency {},
        .stop_token = {},
      });

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

  EXPECT_TRUE(complete_called);
  EXPECT_EQ(completed_id, ImportJobId { 123U });
  EXPECT_FALSE(received_success);
  EXPECT_EQ(canceled_code, "import.canceled");
}

NOLINT_TEST_F(AsyncImporterCancellationTest, CloseJobChannelPreventsSubmissions)
{
  AsyncImporter importer(config_);

  importer.CloseJobChannel();

  EXPECT_FALSE(importer.IsAcceptingJobs());

  JobEntry entry;
  entry.job_id = ImportJobId { 1U };
  EXPECT_FALSE(importer.TrySubmitJob(std::move(entry)));
}

} // namespace
