//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <exception>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferContainerImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>

namespace oxygen::content::import::detail {

namespace {

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    std::string object_path = {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .object_path = std::move(object_path),
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
  auto session = ImportSession(Request(), FileReader(), FileWriter(),
    ThreadPool(), TableRegistry(), IndexRegistry());

  if (!Request().buffer_container.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "buffer.container.request_invalid",
      "BufferContainerImportJob requires request.buffer_container");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid buffer-container request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto descriptor_doc = nlohmann::json {};
  auto parse_exception = std::optional<std::string> {};
  try {
    descriptor_doc = nlohmann::json::parse(
      Request().buffer_container->normalized_descriptor_json);
  } catch (const std::exception& ex) {
    parse_exception = ex.what();
  }
  if (parse_exception.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "buffer.container.request_invalid_json",
      "Normalized descriptor payload is invalid JSON: " + *parse_exception);
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid buffer-container payload");
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (!descriptor_doc.is_object()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "buffer.container.request_invalid_json",
      "Normalized descriptor payload must be a JSON object");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid buffer-container payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto pipeline = BufferPipeline(*ThreadPool(),
    BufferPipeline::Config {
      .queue_capacity = Concurrency().buffer.queue_capacity,
      .worker_count = Concurrency().buffer.workers,
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });
  StartPipeline(pipeline);

  auto submitter
    = BufferImportSubmitter(session, Request(), FileReader(), StopToken());
  const auto buffer_chunks = descriptor_doc.contains("buffers")
    ? descriptor_doc["buffers"]
    : nlohmann::json {};
  const auto submission = co_await submitter.SubmitBufferChunks(
    buffer_chunks, Request().source_path.parent_path(), pipeline);
  pipeline.Close();

  if (submission.submitted_count == 0U) {
    if (!session.HasErrors()) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "buffer.container.no_submissions",
        "No buffer work items were submitted");
    }
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Buffer container failed before pipeline submission");
    co_return co_await FinalizeWithTelemetry(session);
  }

  co_await submitter.CollectAndEmit(pipeline, submission);

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
