//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Detail/ImportJob.h>
#include <Oxygen/OxCo/Algorithms.h>

namespace oxygen::content::import::detail {

namespace {

  [[nodiscard]] auto VirtualMountRootLeaf(const ImportRequest& request)
    -> std::filesystem::path
  {
    auto mount_root
      = std::filesystem::path(request.loose_cooked_layout.virtual_mount_root)
          .lexically_normal();
    auto leaf = mount_root.filename();
    if (!leaf.empty()) {
      return leaf;
    }

    // Defensive fallback: virtual mount roots are expected to end with a
    // directory name (e.g. "/.cooked").
    return std::filesystem::path(".cooked");
  }

  [[nodiscard]] auto ResolveCookedRootForRequest(const ImportRequest& request)
    -> std::filesystem::path
  {
    const auto mount_leaf = VirtualMountRootLeaf(request);

    std::filesystem::path base_root;
    if (request.cooked_root.has_value()) {
      base_root = *request.cooked_root;
    } else if (!request.source_path.empty()) {
      std::error_code ec;
      auto absolute_source = std::filesystem::absolute(request.source_path, ec);
      if (!ec) {
        base_root = absolute_source.parent_path();
      }
    }

    if (base_root.empty()) {
      base_root = std::filesystem::temp_directory_path();
    }

    // Ensure the cooked root ends with the virtual mount root leaf directory
    // (e.g. ".cooked"). This keeps incremental imports and updates stable.
    if (base_root.filename() == mount_leaf) {
      return base_root;
    }

    return base_root / mount_leaf;
  }

} // namespace

ImportJob::ImportJob(ImportJobId job_id, ImportRequest request,
  ImportCompletionCallback on_complete, ImportProgressCallback on_progress,
  std::shared_ptr<co::Event> cancel_event, IAsyncFileWriter& file_writer)
  : job_id_(job_id)
  , request_(std::move(request))
  , on_complete_(std::move(on_complete))
  , on_progress_(std::move(on_progress))
  , cancel_event_(std::move(cancel_event))
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

auto ImportJob::GetJobId() const noexcept -> ImportJobId { return job_id_; }

auto ImportJob::GetName() const noexcept -> std::string_view { return name_; }

void ImportJob::SetName(std::string_view name) noexcept
{
  name_.assign(name.begin(), name.end());
}

auto ImportJob::Request() -> ImportRequest& { return request_; }

auto ImportJob::Request() const -> const ImportRequest& { return request_; }

/*!
 Ensure the request has a concrete cooked root and create it on disk.

 Uses the request's explicit cooked root when provided. Otherwise, derives a
 cooked root from the source path and loose cooked layout. If the source path
 cannot be resolved, falls back to the process temp directory.
*/
auto ImportJob::EnsureCookedRoot() -> void
{
  auto cooked_root = ResolveCookedRootForRequest(request_);
  request_.cooked_root = cooked_root;

  std::error_code ec;
  std::filesystem::create_directories(cooked_root, ec);
  if (ec) {
    DLOG_F(WARNING, "Failed to create cooked root '{}': {}",
      cooked_root.string(), ec.message());
  }
}

auto ImportJob::FileWriter() -> IAsyncFileWriter& { return file_writer_; }

auto ImportJob::JobId() const -> ImportJobId { return job_id_; }

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

    DLOG_F(
      2, "ImportJob finalize: job_id={} success={}", job_id_, report.success);

    if (on_complete_) {
      on_complete_(job_id_, report);
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
        if (cancel_event_ && cancel_event_->Triggered()) {
          stop_source_.request_stop();
          co_return MakeCancelledReport(request_);
        }

        if (cancel_event_) {
          auto [cancelled, maybe_report]
            = co_await co::AnyOf(*cancel_event_, ExecuteAsync());
          if (cancelled.has_value()) {
            stop_source_.request_stop();
            co_return MakeCancelledReport(request_);
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

      DLOG_F(2, "ImportJob main cancelled: job_id={}", job_id_);
      stop_source_.request_stop();
      finalize(MakeCancelledReport(request_));
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
  if (!on_progress_) {
    return;
  }

  ImportProgress progress;
  progress.job_id = job_id_;
  progress.phase = phase;
  progress.overall_progress = overall_progress;
  progress.message = std::move(message);
  on_progress_(progress);
}

} // namespace oxygen::content::import::detail
