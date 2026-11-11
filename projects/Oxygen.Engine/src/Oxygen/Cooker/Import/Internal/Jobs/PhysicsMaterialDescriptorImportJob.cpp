//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/PhysicsMaterialDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/PhysicsMaterialImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import::detail {

namespace {

  struct DescriptorTarget final {
    std::string relpath;
    std::string virtual_path;
  };

  [[nodiscard]] auto MakeDuration(
    const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

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

  //! Places the descriptor at the job's identity.
  [[nodiscard]] auto ResolveDescriptorTarget(const ImportRequest& request,
    ImportSession& session) -> std::optional<DescriptorTarget>
  {
    auto placement = internal::PlaceAsset(
      request, data::kPhysicsMaterialDescriptorExtension);
    if (!placement.has_value()) {
      session.AddDiagnostic(std::move(placement.error()));
      return std::nullopt;
    }
    return DescriptorTarget {
      .relpath = std::move(placement->descriptor_relpath),
      .virtual_path = std::move(placement->virtual_path),
    };
  }

  [[nodiscard]] auto ResolveAssetKey(const ImportRequest& request,
    std::string_view virtual_path) -> data::AssetKey
  {
    static_cast<void>(request);
    return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
  }

} // namespace

auto PhysicsMaterialDescriptorImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting physics material descriptor job: job_id={} path={}",
    JobId(), Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  auto telemetry = ImportTelemetry {};
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

  const auto& payload = Request().physics_material_descriptor;
  if (!payload.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      std::string(import_codes::kPhysicsMaterialRequestInvalid),
      "PhysicsMaterialDescriptorImportJob requires request "
      "physics_material_descriptor payload");
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Invalid physics material descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto material = internal::PreparePhysicsMaterialDescriptor(
    payload->descriptor_json, Request().source_path, diagnostics);
  for (auto& diagnostic : diagnostics) {
    session.AddDiagnostic(std::move(diagnostic));
  }
  if (!material.has_value()) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Invalid physics material descriptor payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto target = ResolveDescriptorTarget(Request(), session);
  if (!target.has_value()) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics material descriptor target resolution failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto descriptor = data::pak::physics::PhysicsMaterialAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kPhysicsMaterial);
  descriptor.header.version = data::pak::physics::kPhysicsMaterialAssetVersion;
  const auto material_name
    = std::filesystem::path(target->relpath).stem().string();

  // A value the descriptor does not author keeps the cooked default.
  descriptor.static_friction
    = material->static_friction.value_or(descriptor.static_friction);
  descriptor.dynamic_friction
    = material->dynamic_friction.value_or(descriptor.dynamic_friction);
  descriptor.restitution
    = material->restitution.value_or(descriptor.restitution);
  descriptor.density = material->density.value_or(descriptor.density);
  descriptor.combine_mode_friction = material->combine_mode_friction.value_or(
    descriptor.combine_mode_friction);
  descriptor.combine_mode_restitution
    = material->combine_mode_restitution.value_or(
      descriptor.combine_mode_restitution);

  auto& pipeline = CreatePipeline<PhysicsMaterialImportPipeline>(
    PhysicsMaterialImportPipeline::Config {
      .queue_capacity = Concurrency().material.queue_capacity,
      .worker_count = Concurrency().material.workers,
    });

  auto item = PhysicsMaterialImportPipeline::WorkItem {
    .source_id = target->virtual_path,
    .name = material_name,
    .descriptor = descriptor,
    .on_started = {},
    .on_finished = {},
    .stop_token = StopToken(),
  };
  co_await pipeline.Submit(std::move(item));
  pipeline.Close();

  auto result = co_await pipeline.Collect();
  if (result.telemetry.cook_duration.has_value()) {
    session.AddCookDuration(*result.telemetry.cook_duration);
  }
  for (auto& diagnostic : result.diagnostics) {
    session.AddDiagnostic(std::move(diagnostic));
  }

  if (!result.success) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics material descriptor processing failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto emit_start = std::chrono::steady_clock::now();
  const auto material_key = ResolveAssetKey(Request(), target->virtual_path);
  session.AssetEmitter().Emit(material_key, data::AssetType::kPhysicsMaterial,
    target->virtual_path, target->relpath, result.descriptor_bytes, {});
  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto PhysicsMaterialDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
