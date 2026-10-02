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
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/MaterialDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/TextureReferenceResolver.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/OxCo/Co.h>

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

  auto AddDiagnostics(
    ImportSession& session, std::vector<ImportDiagnostic> diagnostics) -> void
  {
    for (auto& diagnostic : diagnostics) {
      session.AddDiagnostic(std::move(diagnostic));
    }
  }

} // namespace

auto MaterialDescriptorImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting material descriptor job: job_id={} path={}", JobId(),
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

  const auto& descriptor = Request().material_descriptor;
  if (!descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "material.descriptor.request_invalid",
      "MaterialDescriptorImportJob requires request material_descriptor "
      "payload");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid material descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  auto prepared
    = MaterialSource::FromDescriptor(descriptor->normalized_descriptor_json,
      Request().source_path, Request().job_name.value_or(""), diagnostics);
  AddDiagnostics(session, std::move(diagnostics));
  if (!prepared) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Material descriptor preparation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto item = MaterialPipeline::WorkItem {};
  item.source_id = Request().source_path.string();
  item.request = Request();
  item.naming_service = observer_ptr { &GetNamingService() };
  item.stop_token = StopToken();
  item.material = std::move(*prepared);
  auto parse_failed = false;
  auto* const reader = CookedReader().get();
  if (reader == nullptr) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "material.descriptor.reader_unavailable",
      "Async file reader is not available");
    parse_failed = true;
  } else {
    for (const auto& slot : MaterialSource::TextureSlots()) {
      auto& binding = item.material.textures.*slot.binding;
      if (!binding.assigned) {
        continue;
      }
      const auto index
        = co_await internal::ResolveTextureReference(observer_ptr { &session },
          observer_ptr { &Request() }, observer_ptr { reader },
          {
            .virtual_path = binding.source_id,
            .object_path = "textures." + std::string(slot.name),
            .diagnostic_prefix = "material.descriptor.",
          });
      if (!index) {
        parse_failed = true;
        continue;
      }
      binding.index = index->index.get();
    }
  }

  if (parse_failed || session.HasErrors()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Material descriptor parse failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(
    ImportPhase::kWorking, 0.5F, "Importing material descriptor...");
  auto& pipeline = CreatePipeline<MaterialPipeline>(*ThreadPool(),
    MaterialPipeline::Config {
      .queue_capacity = Concurrency().material.queue_capacity,
      .worker_count = Concurrency().material.workers,
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });

  co_await pipeline.Submit(std::move(item));
  pipeline.Close();

  auto result = co_await pipeline.Collect();
  if (result.telemetry.cook_duration.has_value()) {
    session.AddCookDuration(*result.telemetry.cook_duration);
  }
  if (result.telemetry.load_duration.has_value()) {
    session.AddLoadDuration(*result.telemetry.load_duration);
  }
  if (result.telemetry.io_duration.has_value()) {
    session.AddIoDuration(*result.telemetry.io_duration);
  }

  AddDiagnostics(session, std::move(result.diagnostics));
  if (!result.success || !result.cooked.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Material descriptor import failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto emit_start = std::chrono::steady_clock::now();
  auto& cooked = *result.cooked;
  session.AssetEmitter().Emit(cooked.material_key, data::AssetType::kMaterial,
    cooked.virtual_path, cooked.descriptor_relpath, cooked.descriptor_bytes,
    std::move(cooked.references));
  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto MaterialDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
