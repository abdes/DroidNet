//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <exception>
#include <filesystem>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/PhysicsMaterialDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/PhysicsMaterialImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/AssetKeyUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import::detail {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

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

  auto GetPhysicsMaterialDescriptorValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      out.set_root_schema(json::parse(kPhysicsMaterialDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(ImportSession& session,
    const ImportRequest& request, const json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "physics.material.schema_validation_failed",
      .validation_failed_prefix
      = "Physics material descriptor validation failed: ",
      .validation_overflow_prefix
      = "Physics material descriptor validation emitted ",
      .validator_failure_code = "physics.material.schema_validator_failure",
      .validator_failure_prefix
      = "Physics material descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetPhysicsMaterialDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(code), message, object_path);
      });
  }

  [[nodiscard]] auto ParseCombineMode(const std::string_view value)
    -> data::pak::physics::PhysicsCombineMode
  {
    using data::pak::physics::PhysicsCombineMode;
    if (value == "average") {
      return PhysicsCombineMode::kAverage;
    }
    if (value == "min") {
      return PhysicsCombineMode::kMin;
    }
    if (value == "max") {
      return PhysicsCombineMode::kMax;
    }
    if (value == "multiply") {
      return PhysicsCombineMode::kMultiply;
    }
    DCHECK_F(false, "Unsupported physics combine mode after schema validation");
    return PhysicsCombineMode::kAverage;
  }

  [[nodiscard]] auto ResolveDescriptorTarget(const ImportRequest& request,
    const json& descriptor_doc, const std::filesystem::path& source_path,
    ImportSession& session) -> std::optional<DescriptorTarget>
  {
    auto target = DescriptorTarget {};

    if (descriptor_doc.contains("virtual_path")) {
      target.virtual_path
        = descriptor_doc.at("virtual_path").get<std::string>();
      if (!internal::IsCanonicalVirtualPath(target.virtual_path)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.material.virtual_path_invalid",
          "virtual_path must be canonical", "virtual_path");
        return std::nullopt;
      }
      if (!internal::TryVirtualPathToRelPath(
            request, target.virtual_path, target.relpath)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.material.virtual_path_unmounted",
          "virtual_path is outside mounted cooked roots", "virtual_path");
        return std::nullopt;
      }
      target.relpath = std::filesystem::path(std::move(target.relpath))
                         .lexically_normal()
                         .generic_string();
      return target;
    }

    auto name = std::string {};
    if (request.job_name.has_value() && !request.job_name->empty()) {
      name = *request.job_name;
    } else if (descriptor_doc.contains("name")) {
      name = descriptor_doc.at("name").get<std::string>();
    } else {
      name = source_path.stem().string();
    }
    if (name.empty()) {
      name = "physics_material";
    }

    target.relpath
      = request.loose_cooked_layout.PhysicsMaterialDescriptorRelPath(name);
    target.virtual_path
      = request.loose_cooked_layout.PhysicsMaterialVirtualPath(name);
    return target;
  }

  [[nodiscard]] auto ResolveAssetKey(const ImportRequest& request,
    std::string_view virtual_path) -> data::AssetKey
  {
    switch (request.options.asset_key_policy) {
    case AssetKeyPolicy::kRandom:
      return util::MakeRandomAssetKey();
    case AssetKeyPolicy::kDeterministicFromVirtualPath:
      return util::MakeDeterministicAssetKey(virtual_path);
    }
    return util::MakeDeterministicAssetKey(virtual_path);
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
  auto session = ImportSession(Request(), FileReader(), FileWriter(),
    ThreadPool(), TableRegistry(), IndexRegistry());

  if (!Request().physics_material_descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.material.request_invalid",
      "PhysicsMaterialDescriptorImportJob requires request "
      "physics_material_descriptor payload");
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Invalid physics material descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto descriptor_doc = json {};
  auto parse_exception = std::optional<std::string> {};
  try {
    descriptor_doc = json::parse(
      Request().physics_material_descriptor->normalized_descriptor_json);
  } catch (const std::exception& ex) {
    parse_exception = ex.what();
  }
  if (parse_exception.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.material.request_invalid_json",
      "Normalized descriptor payload is invalid JSON: " + *parse_exception);
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid physics material payload");
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (!descriptor_doc.is_object()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.material.request_invalid_json",
      "Normalized descriptor payload must be a JSON object");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid physics material payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!ValidateDescriptorSchema(session, Request(), descriptor_doc)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics material descriptor schema validation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto target = ResolveDescriptorTarget(
    Request(), descriptor_doc, Request().source_path, session);
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
  util::TruncateAndNullTerminate(
    descriptor.header.name, sizeof(descriptor.header.name), material_name);

  if (descriptor_doc.contains("friction")) {
    descriptor.friction = descriptor_doc.at("friction").get<float>();
  }
  if (descriptor_doc.contains("restitution")) {
    descriptor.restitution = descriptor_doc.at("restitution").get<float>();
  }
  if (descriptor_doc.contains("density")) {
    descriptor.density = descriptor_doc.at("density").get<float>();
  }
  if (descriptor_doc.contains("combine_mode_friction")) {
    descriptor.combine_mode_friction = ParseCombineMode(
      descriptor_doc.at("combine_mode_friction").get<std::string>());
  }
  if (descriptor_doc.contains("combine_mode_restitution")) {
    descriptor.combine_mode_restitution = ParseCombineMode(
      descriptor_doc.at("combine_mode_restitution").get<std::string>());
  }

  auto pipeline = PhysicsMaterialImportPipeline(*ThreadPool(),
    PhysicsMaterialImportPipeline::Config {
      .queue_capacity = Concurrency().material.queue_capacity,
      .worker_count = Concurrency().material.workers,
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });
  StartPipeline(pipeline);

  auto item = PhysicsMaterialImportPipeline::WorkItem {
    .source_id = target->virtual_path,
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
    target->virtual_path, target->relpath, result.descriptor_bytes);
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
