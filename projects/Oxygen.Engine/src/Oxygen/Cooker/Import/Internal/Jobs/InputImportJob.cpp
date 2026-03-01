//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/InputImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/InputImportPipeline.h>

namespace oxygen::content::import::detail {

namespace {

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message)
    -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
    });
  }

} // namespace

auto InputImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting input import job: job_id={} path={}", JobId(),
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

  if (!Request().options.input.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "input.request.invalid_job_type",
      "InputImportJob requires options.input to be present");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid input import options");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kLoading, 0.0F, "Loading input source...");
  const auto load_start = std::chrono::steady_clock::now();
  auto source = co_await LoadSource(session);
  const auto load_end = std::chrono::steady_clock::now();
  session.AddSourceLoadDuration(MakeDuration(load_start, load_end));
  if (!source.success) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F, "Input source load failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kWorking, 0.5F, "Importing input assets...");

  auto pipeline = InputImportPipeline(InputImportPipeline::Config {
    .queue_capacity = Concurrency().scene.queue_capacity,
    .worker_count = Concurrency().scene.workers,
  });
  StartPipeline(pipeline);

  co_await pipeline.Submit(InputImportPipeline::WorkItem {
    .source_id = Request().source_path.string(),
    .source_bytes = std::move(source.bytes),
    .session = make_observer(&session),
    .stop_token = StopToken(),
  });
  pipeline.Close();

  auto result = co_await pipeline.Collect();
  if (result.telemetry.cook_duration.has_value()) {
    session.AddCookDuration(*result.telemetry.cook_duration);
  }

  if (!result.success) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F, "Input import failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto InputImportJob::LoadSource(ImportSession& session) -> co::Co<LoadedSource>
{
  const auto& request = Request();
  auto* const reader = FileReader().get();
  if (reader == nullptr) {
    AddDiagnostic(session, request, ImportSeverity::kError,
      "input.import.reader_unavailable", "Async file reader is not available");
    co_return LoadedSource {
      .success = false,
    };
  }

  const auto read = co_await reader->ReadFile(request.source_path);
  if (!read.has_value()) {
    AddDiagnostic(session, request, ImportSeverity::kError,
      "input.import.source_read_failed",
      "Failed to read source file: " + read.error().ToString());
    co_return LoadedSource {
      .success = false,
    };
  }

  co_return LoadedSource {
    .success = true,
    .bytes = read.value(),
  };
}

auto InputImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
