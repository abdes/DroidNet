//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Detail/AsyncImporter.h>
#include <Oxygen/Content/Import/Async/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Layout.h>
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

  auto EnsureCookedRoot(ImportRequest& request) -> void
  {
    auto cooked_root = ResolveCookedRootForRequest(request);
    request.cooked_root = cooked_root;

    std::error_code ec;
    std::filesystem::create_directories(cooked_root, ec);
    if (ec) {
      DLOG_F(WARNING, "Failed to create cooked root '{}': {}",
        cooked_root.string(), ec.message());
    }
  }

} // namespace

AsyncImporter::AsyncImporter(Config config)
  : job_channel_(config.channel_capacity)
  , config_(config)
{
  DLOG_F(INFO, "AsyncImporter created with channel capacity {}",
    config.channel_capacity);
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
  return job_channel_.TrySend(std::move(entry));
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
    // Receive next job (suspends until available or channel closed)
    auto maybe_entry = co_await job_channel_.Receive();

    if (!maybe_entry.has_value()) {
      // Channel closed and empty - exit the loop
      DLOG_F(INFO, "Job channel closed, exiting processing loop");
      break;
    }

    // Process the job
    co_await ProcessJob(std::move(*maybe_entry));
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
  DLOG_F(INFO, "Processing job {}: {}", entry.job_id,
    entry.request.source_path.string());

  // Ensure the job has a usable cooked root. Tests and callers may submit
  // requests without a cooked_root; the session needs a concrete directory
  // to write the container index.
  EnsureCookedRoot(entry.request);

  // Check for early cancellation
  if (entry.cancel_event && entry.cancel_event->Triggered()) {
    DLOG_F(INFO, "Job {} cancelled before processing", entry.job_id);
    if (entry.on_cancel) {
      entry.on_cancel(entry.job_id);
    }
    co_return;
  }

  // Report starting progress
  if (entry.on_progress) {
    ImportProgress progress;
    progress.job_id = entry.job_id;
    progress.phase = ImportPhase::kParsing;
    progress.overall_progress = 0.0f;
    progress.message = "Starting import...";
    entry.on_progress(progress);
  }

  if (config_.file_writer == nullptr) {
    ImportReport report {
      .cooked_root = entry.request.cooked_root.value_or(
        entry.request.source_path.parent_path()),
      .success = false,
    };
    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = "import.no_file_writer",
      .message = "AsyncImporter has no IAsyncFileWriter configured",
      .source_path = entry.request.source_path.string(),
    });

    if (entry.on_complete) {
      entry.on_complete(entry.job_id, report);
    }
    co_return;
  }

  // Create per-job session.
  ImportSession session(entry.request, *config_.file_writer);

  // TODO: Phase 4.4+ - Backend integration.
  // For now, we only exercise session creation and finalization.

  // Check for cancellation after "processing"
  if (entry.cancel_event && entry.cancel_event->Triggered()) {
    DLOG_F(INFO, "Job {} cancelled during processing", entry.job_id);
    if (entry.on_cancel) {
      entry.on_cancel(entry.job_id);
    }
    co_return;
  }

  if (entry.on_progress) {
    ImportProgress progress;
    progress.job_id = entry.job_id;
    progress.phase = ImportPhase::kWriting;
    progress.overall_progress = 0.9f;
    progress.message = "Finalizing import...";
    entry.on_progress(progress);
  }

  auto report = co_await session.Finalize();

  if (entry.on_progress) {
    ImportProgress progress;
    progress.job_id = entry.job_id;
    progress.phase
      = report.success ? ImportPhase::kComplete : ImportPhase::kFailed;
    progress.overall_progress = 1.0f;
    progress.message = report.success ? "Import complete" : "Import failed";
    entry.on_progress(progress);
  }

  DLOG_F(INFO, "Job {} completed (success={})", entry.job_id, report.success);

  if (entry.on_complete) {
    entry.on_complete(entry.job_id, report);
  }
}

} // namespace oxygen::content::import::detail
