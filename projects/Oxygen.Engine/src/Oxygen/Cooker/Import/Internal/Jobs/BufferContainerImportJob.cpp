//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferContainerImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import::detail {

namespace {

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    std::string json_pointer = {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .json_pointer = std::move(json_pointer),
    });
  }

} // namespace

auto BufferContainerImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting buffer-container job: job_id={} path={}", JobId(),
    Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  auto telemetry = ImportTelemetry {};
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
  auto& session = Session();

  const auto& payload = Request().buffer_container;
  if (!payload.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      std::string(import_codes::kBufferContainerRequestInvalid),
      "BufferContainerImportJob requires request.buffer_container");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid buffer-container request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  CHECK_NOTNULL_F(Request().source_mounts.get(),
    "A buffer container locates its sources through the job's mounts");
  const auto sources = internal::BufferSource::FromContainer(
    payload->descriptor_json, Request().source_path, Request().OutputIdentity(),
    *Request().source_mounts, diagnostics);
  for (auto& diagnostic : diagnostics) {
    session.AddDiagnostic(std::move(diagnostic));
  }
  if (!sources.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Buffer container preparation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto& pipeline = CreatePipeline<BufferPipeline>(*ThreadPool(),
    BufferPipeline::Config {
      .queue_capacity = Concurrency().buffer.queue_capacity,
      .worker_count = Concurrency().buffer.workers,
    });

  auto submitter
    = BufferImportSubmitter(session, Request(), FileReader(), StopToken());
  const auto submission = co_await submitter.SubmitBuffers(*sources, pipeline);
  pipeline.Close();

  if (submission.submitted_count == 0U) {
    if (!session.HasErrors()) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        std::string(import_codes::kBufferContainerNoSubmissions),
        "No buffer work items were submitted");
    }
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Buffer container failed before pipeline submission");
    co_return co_await FinalizeWithTelemetry(session);
  }

  [[maybe_unused]] const auto emitted_buffers
    = co_await submitter.CollectAndEmit(pipeline, submission);

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto BufferContainerImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
