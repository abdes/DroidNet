//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Detail/ImportJob.h>
#include <Oxygen/OxCo/Algorithms.h>

namespace oxygen::content::import::detail {

ImportJob::ImportJob(JobEntry entry, IAsyncFileWriter& file_writer)
  : entry_(std::move(entry))
  , file_writer_(file_writer)
{
}

auto ImportJob::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  return co::OpenNursery(nursery_, std::move(started));
}

void ImportJob::Run()
{
  DCHECK_F(
    nursery_ != nullptr, "ImportJob::Run() called before ActivateAsync()");
  DCHECK_F(!started_, "ImportJob::Run() called more than once");
  started_ = true;

  nursery_->Start([this]() -> co::Co<> { co_await MainAsync(); });
}

void ImportJob::Stop()
{
  stop_source_.request_stop();
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }
}

auto ImportJob::IsRunning() const -> bool { return nursery_ != nullptr; }

auto ImportJob::Wait() -> co::Co<> { co_await completed_; }

auto ImportJob::Request() -> ImportRequest& { return entry_.request; }

auto ImportJob::Request() const -> const ImportRequest&
{
  return entry_.request;
}

auto ImportJob::FileWriter() -> IAsyncFileWriter& { return file_writer_; }

auto ImportJob::JobId() const -> ImportJobId { return entry_.job_id; }

auto ImportJob::StopToken() const noexcept -> std::stop_token
{
  return stop_source_.get_token();
}

auto ImportJob::MainAsync() -> co::Co<>
{
  bool finalized = false;

  auto finalize = [&](ImportReport report) {
    if (finalized) {
      return;
    }
    finalized = true;

    DLOG_F(2, "ImportJob finalize: job_id={} success={}", entry_.job_id,
      report.success);

    if (entry_.on_complete) {
      entry_.on_complete(entry_.job_id, report);
    }

    completed_.Trigger();

    // Close the job nursery after reporting completion. This lets the parent
    // importer await job completion by joining the ActivateAsync task.
    Stop();
  };

  // Guarantee: call on_complete exactly once, even if this coroutine is
  // cancelled by importer shutdown. Note that code after a cancellable
  // await is not guaranteed to run, so finalization must be done inside the
  // branches.
  co_await co::AnyOf(
    [&]() -> co::Co<> {
      auto run_work = [&]() -> co::Co<ImportReport> {
        if (entry_.cancel_event && entry_.cancel_event->Triggered()) {
          stop_source_.request_stop();
          co_return MakeCancelledReport(entry_.request);
        }

        if (entry_.cancel_event) {
          auto [cancelled, maybe_report]
            = co_await co::AnyOf(*entry_.cancel_event, ExecuteAsync());
          if (cancelled.has_value()) {
            stop_source_.request_stop();
            co_return MakeCancelledReport(entry_.request);
          }

          DCHECK_F(maybe_report.has_value());
          co_return std::move(*maybe_report);
        }

        co_return co_await ExecuteAsync();
      };

      finalize(co_await run_work());
      co_return;
    }(),
    co::UntilCancelledAnd([&]() -> co::Co<> {
      if (finalized) {
        co_return;
      }

      DLOG_F(2, "ImportJob main cancelled: job_id={}", entry_.job_id);
      stop_source_.request_stop();
      finalize(MakeCancelledReport(entry_.request));
      co_return;
    }));

  co_return;
}

auto ImportJob::MakeCancelledReport(const ImportRequest& request) const
  -> ImportReport
{
  ImportReport report {
    .cooked_root
    = request.cooked_root.value_or(request.source_path.parent_path()),
    .success = false,
  };

  report.diagnostics.push_back({
    .severity = ImportSeverity::kInfo,
    .code = "import.cancelled",
    .message = "Import cancelled",
    .source_path = request.source_path.string(),
  });

  return report;
}

auto ImportJob::MakeNoFileWriterReport(const ImportRequest& request) const
  -> ImportReport
{
  ImportReport report {
    .cooked_root
    = request.cooked_root.value_or(request.source_path.parent_path()),
    .success = false,
  };

  report.diagnostics.push_back({
    .severity = ImportSeverity::kError,
    .code = "import.no_file_writer",
    .message = "AsyncImporter has no IAsyncFileWriter configured",
    .source_path = request.source_path.string(),
  });

  return report;
}

auto ImportJob::ReportProgress(
  ImportPhase phase, float overall_progress, std::string message) -> void
{
  if (!entry_.on_progress) {
    return;
  }

  ImportProgress progress;
  progress.job_id = entry_.job_id;
  progress.phase = phase;
  progress.overall_progress = overall_progress;
  progress.message = std::move(message);
  entry_.on_progress(progress);
}

} // namespace oxygen::content::import::detail
