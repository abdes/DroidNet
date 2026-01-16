//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Async/Jobs/AudioImportJob.h>

namespace oxygen::content::import::detail {

/*!
 Execute a standalone audio import workflow.

 The current implementation wires the job lifecycle and progress reporting.
 Phase 6 will populate the load/cook/emit stages with real pipeline work.
*/
auto AudioImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "AudioImportJob starting: job_id={} path={}", JobId(),
    Request().source_path.string());

  EnsureCookedRoot();

  ImportSession session(
    Request(), FileReader(), FileWriter(), ThreadPool(), TableRegistry());

  ReportProgress(ImportPhase::kParsing, 0.0f, "Loading audio source...");
  const auto source = co_await LoadSource(session);
  if (!source.success) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Audio load failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kGeometry, 0.4f, "Cooking audio...");
  if (!co_await CookAudio(source, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Audio cook failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kWriting, 0.7f, "Emitting audio...");
  if (!co_await EmitAudio(source, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Audio emit failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kWriting, 0.9f, "Finalizing import...");
  auto report = co_await FinalizeSession(session);

  ReportProgress(report.success ? ImportPhase::kComplete : ImportPhase::kFailed,
    1.0f, report.success ? "Import complete" : "Import failed");

  co_return report;
}

//! Load the audio bytes from disk or embedded data.
auto AudioImportJob::LoadSource([[maybe_unused]] ImportSession& session)
  -> co::Co<AudioSource>
{
  // TODO(Phase 6): Read audio bytes via IAsyncFileReader.
  // TODO(Phase 6): Honor StopToken() to support cancellation.
  co_return AudioSource {
    .success = true,
  };
}

//! Cook the audio via the async AudioPipeline.
auto AudioImportJob::CookAudio([[maybe_unused]] const AudioSource& source,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 6): Submit work item to AudioPipeline.
  // TODO(Phase 6): Collect cooked payload and store for emission.
  co_return true;
}

//! Emit the cooked audio via the future AudioEmitter.
auto AudioImportJob::EmitAudio([[maybe_unused]] const AudioSource& source,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 6): Emit cooked payload via AudioEmitter.
  co_return true;
}

//! Finalize the session and return the import report.
auto AudioImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
