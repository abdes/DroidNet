//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include <Oxygen/Content/Import/Async/Detail/ImportJob.h>
#include <Oxygen/Content/Import/Async/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Async/ImportEventLoop.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::content::import::detail;
using oxygen::co::Co;
using oxygen::co::Event;
using oxygen::co::kJoin;

namespace {

[[nodiscard]] auto MakeSuccessReport() -> ImportReport
{
  return ImportReport {
    .cooked_root = std::filesystem::temp_directory_path(),
    .success = true,
  };
}

class ImportJobTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
  std::unique_ptr<IAsyncFileWriter> file_writer_;

  void SetUp() override { file_writer_ = CreateAsyncFileWriter(loop_); }
};

class ImmediateSuccessJob final : public ImportJob {
public:
  using ImportJob::ImportJob;

  bool executed = false;

private:
  [[nodiscard]] auto ExecuteAsync() -> Co<ImportReport> override
  {
    executed = true;
    co_return MakeSuccessReport();
  }
};

class BlockingJob final : public ImportJob {
public:
  using ImportJob::ImportJob;

  [[nodiscard]] auto StopTokenForTest() const noexcept -> std::stop_token
  {
    return StopToken();
  }

  Event started;
  Event waiting;
  bool cancelled_cleanup_ran = false;
  bool executed = false;

private:
  [[nodiscard]] auto ExecuteAsync() -> Co<ImportReport> override
  {
    executed = true;
    started.Trigger();

    waiting.Trigger();

    co_await oxygen::co::AnyOf(oxygen::co::kSuspendForever,
      oxygen::co::UntilCancelledAnd([this]() -> Co<> {
        cancelled_cleanup_ran = true;
        co_return;
      }));

    co_return MakeSuccessReport();
  }
};

class StartTaskJob final : public ImportJob {
public:
  using ImportJob::ImportJob;

  Event task_started;

private:
  [[nodiscard]] auto ExecuteAsync() -> Co<ImportReport> override
  {
    StartTask([this]() -> Co<> {
      task_started.Trigger();
      co_return;
    });

    co_await task_started;
    co_return MakeSuccessReport();
  }
};

struct FakePipeline {
  bool started = false;

  auto Start(oxygen::co::Nursery& nursery) -> void
  {
    started = true;
    nursery.Start([&]() -> Co<> { co_return; });
  }
};

class StartPipelineJob final : public ImportJob {
public:
  using ImportJob::ImportJob;

  FakePipeline* pipeline = nullptr;

private:
  [[nodiscard]] auto ExecuteAsync() -> Co<ImportReport> override
  {
    DCHECK_F(pipeline != nullptr);
    StartPipeline(*pipeline);
    co_return MakeSuccessReport();
  }
};

//! Verify a job runs and invokes on_complete exactly once.
NOLINT_TEST_F(ImportJobTest, ImportJob_Run_CompletesAndCallsOnCompleteOnce)
{
  // Arrange
  std::atomic<int> complete_calls { 0 };
  bool reported_success = false;
  Event done;

  JobEntry entry;
  entry.job_id = 1;
  entry.request.source_path = "test.txt";
  entry.on_complete = [&](ImportJobId, const ImportReport& report) {
    reported_success = report.success;
    ++complete_calls;
    done.Trigger();
  };

  ImmediateSuccessJob job(std::move(entry), *file_writer_);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ImportJob::ActivateAsync, &job);
      job.Run();

      co_await done;
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_EQ(complete_calls.load(), 1);
  EXPECT_TRUE(reported_success);
}

//! Verify Stop cancels a running job and completion is reported exactly once.
NOLINT_TEST_F(ImportJobTest, ImportJob_Stop_CompletesWithCancelledDiagnostic)
{
  // Arrange
  std::atomic<int> complete_calls { 0 };
  bool reported_success = true;
  std::string cancelled_code;
  Event done;

  JobEntry entry;
  entry.job_id = 2;
  entry.request.source_path = "test.txt";
  entry.on_complete = [&](ImportJobId, const ImportReport& report) {
    reported_success = report.success;
    if (!report.diagnostics.empty()) {
      cancelled_code = report.diagnostics.front().code;
    }
    ++complete_calls;
    done.Trigger();
  };

  BlockingJob job(std::move(entry), *file_writer_);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ImportJob::ActivateAsync, &job);
      job.Run();

      co_await job.waiting;
      job.Stop();

      co_await done;
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_EQ(complete_calls.load(), 1);
  EXPECT_FALSE(reported_success);
  EXPECT_EQ(cancelled_code, "import.cancelled");
  EXPECT_TRUE(job.StopTokenForTest().stop_requested());
}

//! Verify pre-triggered cancel_event completes as cancelled and avoids work.
NOLINT_TEST_F(ImportJobTest, ImportJob_CancelEvent_PreTriggered_AvoidsExecution)
{
  // Arrange
  std::atomic<int> complete_calls { 0 };
  bool reported_success = true;
  std::string cancelled_code;
  Event done;

  auto cancel_event = std::make_shared<Event>();
  cancel_event->Trigger();

  JobEntry entry;
  entry.job_id = 3;
  entry.request.source_path = "test.txt";
  entry.cancel_event = cancel_event;
  entry.on_complete = [&](ImportJobId, const ImportReport& report) {
    reported_success = report.success;
    if (!report.diagnostics.empty()) {
      cancelled_code = report.diagnostics.front().code;
    }
    ++complete_calls;
    done.Trigger();
  };

  BlockingJob job(std::move(entry), *file_writer_);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ImportJob::ActivateAsync, &job);
      job.Run();

      co_await done;
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_EQ(complete_calls.load(), 1);
  EXPECT_FALSE(reported_success);
  EXPECT_EQ(cancelled_code, "import.cancelled");
  EXPECT_FALSE(job.executed);
  EXPECT_TRUE(job.StopTokenForTest().stop_requested());
}

//! Verify StartTask schedules work within the job scope.
NOLINT_TEST_F(ImportJobTest, ImportJob_StartTask_ExecutesTask)
{
  // Arrange
  std::atomic<int> complete_calls { 0 };
  Event done;

  JobEntry entry;
  entry.job_id = 4;
  entry.request.source_path = "test.txt";
  entry.on_complete = [&](ImportJobId, const ImportReport&) {
    ++complete_calls;
    done.Trigger();
  };

  StartTaskJob job(std::move(entry), *file_writer_);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ImportJob::ActivateAsync, &job);
      job.Run();

      co_await done;
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_EQ(complete_calls.load(), 1);
}

//! Verify StartPipeline invokes the pipeline Start() within the job scope.
NOLINT_TEST_F(ImportJobTest, ImportJob_StartPipeline_StartsWorkers)
{
  // Arrange
  std::atomic<int> complete_calls { 0 };
  Event done;

  FakePipeline pipeline;

  JobEntry entry;
  entry.job_id = 5;
  entry.request.source_path = "test.txt";
  entry.on_complete = [&](ImportJobId, const ImportReport&) {
    ++complete_calls;
    done.Trigger();
  };

  StartPipelineJob job(std::move(entry), *file_writer_);
  job.pipeline = &pipeline;

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ImportJob::ActivateAsync, &job);
      job.Run();

      co_await done;
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_EQ(complete_calls.load(), 1);
  EXPECT_TRUE(pipeline.started);
}

} // namespace
