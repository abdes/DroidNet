//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Internal/ImportSession.h>
#include <Oxygen/Content/Import/Internal/Jobs/AudioImportJob.h>

namespace oxygen::content::import::detail {

/*!
 Execute a standalone audio import workflow.

 The current implementation wires the job lifecycle and progress reporting.
 Phase 6 will populate the load/cook/emit stages with real pipeline work.
*/
auto AudioImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting job: job_id={} path={}", JobId(),
    Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  ImportTelemetry telemetry {
    .io_duration = std::chrono::microseconds { 0 },
    .source_load_duration = std::chrono::microseconds { 0 },
    .decode_duration = std::chrono::microseconds { 0 },
    .load_duration = std::chrono::microseconds { 0 },
    .cook_duration = std::chrono::microseconds { 0 },
    .emit_duration = std::chrono::microseconds { 0 },
    .finalize_duration = std::chrono::microseconds { 0 },
    .total_duration = std::chrono::microseconds { 0 },
  };
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };
  const auto FinalizeWithTelemetry
    = [&](ImportSession& session) -> co::Co<ImportReport> {
    const auto finalize_start = std::chrono::steady_clock::now();
    auto report = co_await FinalizeSession(session);
    const auto finalize_end = std::chrono::steady_clock::now();
    telemetry.finalize_duration = MakeDuration(finalize_start, finalize_end);
    telemetry.total_duration = MakeDuration(job_start, finalize_end);
    telemetry.io_duration = session.IoDuration();
    telemetry.source_load_duration = session.SourceLoadDuration();
    telemetry.decode_duration = session.DecodeDuration();
    telemetry.load_duration
      = session.SourceLoadDuration() + session.LoadDuration();
    telemetry.cook_duration = session.CookDuration();
    telemetry.emit_duration = session.EmitDuration();
    report.telemetry = telemetry;
    co_return report;
  };

  EnsureCookedRoot();

  ImportSession session(Request(), FileReader(), FileWriter(), ThreadPool(),
    TableRegistry(), IndexRegistry());

  ReportPhaseProgress(ImportPhase::kLoading, 0.0f, "Loading audio source...");
  const auto load_start = std::chrono::steady_clock::now();
  const auto source = co_await LoadSource(session);
  const auto load_end = std::chrono::steady_clock::now();
  session.AddSourceLoadDuration(MakeDuration(load_start, load_end));
  if (!source.success) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0f, "Audio load failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kWorking, 0.4f, "Cooking audio...");
  const auto cook_start = std::chrono::steady_clock::now();
  const bool cooked = co_await CookAudio(source, session);
  const auto cook_end = std::chrono::steady_clock::now();
  session.AddCookDuration(MakeDuration(cook_start, cook_end));
  if (!cooked) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0f, "Audio cook failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kFinalizing, 0.7f, "Emitting audio...");
  const auto emit_start = std::chrono::steady_clock::now();
  const bool emitted = co_await EmitAudio(source, session);
  const auto emit_end = std::chrono::steady_clock::now();
  session.AddEmitDuration(MakeDuration(emit_start, emit_end));
  if (!emitted) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0f, "Audio emit failed");
    co_return co_await FinalizeWithTelemetry(session);
  }
  auto report = co_await FinalizeWithTelemetry(session);

  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0f,
    report.success ? "Import complete" : "Import failed");

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
