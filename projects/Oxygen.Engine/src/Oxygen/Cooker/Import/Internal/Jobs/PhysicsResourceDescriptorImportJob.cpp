//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/PhysicsResourceDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/PhysicsResourceImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/PhysicsResourceDescriptorSidecar.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>

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

  auto GetPhysicsResourceDescriptorValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      out.set_root_schema(json::parse(kPhysicsResourceDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(ImportSession& session,
    const ImportRequest& request, const json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "physics.resource.schema_validation_failed",
      .validation_failed_prefix
      = "Physics resource descriptor validation failed: ",
      .validation_overflow_prefix
      = "Physics resource descriptor validation emitted ",
      .validator_failure_code = "physics.resource.schema_validator_failure",
      .validator_failure_prefix
      = "Physics resource descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetPhysicsResourceDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(code), message, object_path);
      });
  }

  [[nodiscard]] auto ParseResourceFormat(const std::string_view value)
    -> data::pak::physics::PhysicsResourceFormat
  {
    using data::pak::physics::PhysicsResourceFormat;
    if (value == "jolt_shape_binary") {
      return PhysicsResourceFormat::kJoltShapeBinary;
    }
    if (value == "jolt_constraint_binary") {
      return PhysicsResourceFormat::kJoltConstraintBinary;
    }
    if (value == "jolt_soft_body_shared_settings_binary") {
      return PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary;
    }
    if (value == "jolt_vehicle_constraint_binary") {
      return PhysicsResourceFormat::kJoltVehicleConstraintBinary;
    }
    DCHECK_F(
      false, "Unsupported physics resource format after schema validation");
    return PhysicsResourceFormat::kJoltShapeBinary;
  }

  [[nodiscard]] auto NormalizeRelPath(std::string relpath) -> std::string
  {
    return std::filesystem::path(std::move(relpath))
      .lexically_normal()
      .generic_string();
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
          "physics.resource.virtual_path_invalid",
          "virtual_path must be canonical", "virtual_path");
        return std::nullopt;
      }
      if (!internal::TryVirtualPathToRelPath(
            request, target.virtual_path, target.relpath)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.resource.virtual_path_unmounted",
          "virtual_path is outside mounted cooked roots", "virtual_path");
        return std::nullopt;
      }
      target.relpath = NormalizeRelPath(std::move(target.relpath));
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
      name = "physics_resource";
    }

    target.relpath
      = request.loose_cooked_layout.PhysicsResourceDescriptorRelPath(name);
    target.virtual_path
      = request.loose_cooked_layout.PhysicsResourceVirtualPath(name);
    return target;
  }

  [[nodiscard]] auto ReadBinaryFile(const std::filesystem::path& path)
    -> std::optional<std::vector<std::byte>>
  {
    auto in = std::ifstream(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
      return std::nullopt;
    }
    const auto end = in.tellg();
    if (end < 0) {
      return std::nullopt;
    }

    const auto size = static_cast<size_t>(end);
    in.seekg(0, std::ios::beg);
    auto bytes = std::vector<std::byte>(size);
    if (size > 0U) {
      in.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(size));
      if (!in) {
        return std::nullopt;
      }
    }
    return bytes;
  }

  [[nodiscard]] auto LoadCanonicalPhysicsSidecarRelpathsByIndex(
    const std::filesystem::path& cooked_root)
    -> std::unordered_map<uint32_t, std::string>
  {
    auto sidecar_paths = std::vector<std::filesystem::path> {};
    auto ec = std::error_code {};
    if (!std::filesystem::exists(cooked_root, ec)) {
      return {};
    }

    for (auto it
      = std::filesystem::recursive_directory_iterator(cooked_root, ec);
      !ec && it != std::filesystem::recursive_directory_iterator {};
      it.increment(ec)) {
      if (ec) {
        break;
      }
      if (!it->is_regular_file(ec)) {
        continue;
      }
      if (it->path().extension() != ".opres") {
        continue;
      }
      sidecar_paths.push_back(it->path());
    }

    std::ranges::sort(sidecar_paths, [](const auto& lhs, const auto& rhs) {
      return lhs.generic_string() < rhs.generic_string();
    });

    auto by_index = std::unordered_map<uint32_t, std::string> {};
    for (const auto& path : sidecar_paths) {
      const auto bytes = ReadBinaryFile(path);
      if (!bytes.has_value()) {
        continue;
      }

      auto parsed = internal::ParsedPhysicsResourceDescriptorSidecar {};
      auto parse_error = std::string {};
      if (!internal::ParsePhysicsResourceDescriptorSidecar(
            *bytes, parsed, parse_error)) {
        continue;
      }

      const auto relpath = std::filesystem::relative(path, cooked_root, ec);
      if (ec) {
        ec.clear();
        continue;
      }

      by_index.try_emplace(parsed.resource_index.get(),
        NormalizeRelPath(relpath.generic_string()));
    }
    return by_index;
  }

  [[nodiscard]] auto IsEquivalentDescriptor(
    const data::pak::physics::PhysicsResourceDesc& lhs,
    const data::pak::physics::PhysicsResourceDesc& rhs) -> bool
  {
    return lhs.format == rhs.format && lhs.size_bytes == rhs.size_bytes
      && lhs.content_hash == rhs.content_hash;
  }

  auto ValidateExistingSidecarIfPresent(const std::filesystem::path& full_path,
    const data::pak::physics::PhysicsResourceDesc& descriptor,
    ImportSession& session, const ImportRequest& request,
    std::string_view object_path) -> bool
  {
    if (!std::filesystem::exists(full_path)) {
      return true;
    }

    const auto bytes = ReadBinaryFile(full_path);
    if (!bytes.has_value()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.resource.sidecar_read_failed",
        "Failed reading existing physics resource sidecar: "
          + full_path.string(),
        std::string(object_path));
      return false;
    }

    auto parsed = internal::ParsedPhysicsResourceDescriptorSidecar {};
    auto parse_error = std::string {};
    if (!internal::ParsePhysicsResourceDescriptorSidecar(
          *bytes, parsed, parse_error)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.resource.sidecar_invalid",
        "Existing sidecar is invalid: " + parse_error,
        std::string(object_path));
      return false;
    }

    if (!IsEquivalentDescriptor(parsed.descriptor, descriptor)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.resource.virtual_path_collision",
        "virtual_path already exists with different resource descriptor",
        std::string(object_path));
      return false;
    }
    return true;
  }

} // namespace

auto PhysicsResourceDescriptorImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting physics resource descriptor job: job_id={} path={}",
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

  if (!Request().physics_resource_descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.request_invalid",
      "PhysicsResourceDescriptorImportJob requires request "
      "physics_resource_descriptor payload");
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Invalid physics resource descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto descriptor_doc = json {};
  auto parse_exception = std::optional<std::string> {};
  try {
    descriptor_doc = json::parse(
      Request().physics_resource_descriptor->normalized_descriptor_json);
  } catch (const std::exception& ex) {
    parse_exception = ex.what();
  }
  if (parse_exception.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.request_invalid_json",
      "Normalized descriptor payload is invalid JSON: " + *parse_exception);
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid physics resource payload");
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (!descriptor_doc.is_object()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.request_invalid_json",
      "Normalized descriptor payload must be a JSON object");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid physics resource payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!ValidateDescriptorSchema(session, Request(), descriptor_doc)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics resource descriptor schema validation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto format
    = ParseResourceFormat(descriptor_doc.at("format").get<std::string>());

  auto source_path
    = std::filesystem::path(descriptor_doc.at("source").get<std::string>());
  if (source_path.is_relative()) {
    source_path = Request().source_path.parent_path() / source_path;
  }
  source_path = source_path.lexically_normal();

  const auto target
    = ResolveDescriptorTarget(Request(), descriptor_doc, source_path, session);
  if (!target.has_value()) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics resource descriptor target resolution failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (FileReader() == nullptr) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.reader_unavailable",
      "Async file reader is not available");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Physics resource reader unavailable");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto load_start = std::chrono::steady_clock::now();
  auto source_bytes_result = co_await FileReader()->ReadFile(source_path);
  session.AddSourceLoadDuration(
    MakeDuration(load_start, std::chrono::steady_clock::now()));
  if (!source_bytes_result.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.source_read_failed",
      "Failed reading physics resource source: "
        + source_bytes_result.error().ToString(),
      "source");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Physics resource source load failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (source_bytes_result.value().size()
    > (std::numeric_limits<uint32_t>::max)()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.source_too_large",
      "Physics resource source exceeds maximum supported size of 4 GiB",
      "source");
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics resource source size exceeds limits");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto pipeline = PhysicsResourceImportPipeline(*ThreadPool(),
    PhysicsResourceImportPipeline::Config {
      .queue_capacity = Concurrency().buffer.queue_capacity,
      .worker_count = Concurrency().buffer.workers,
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });
  StartPipeline(pipeline);

  auto item = PhysicsResourceImportPipeline::WorkItem {
    .source_id = target->virtual_path,
    .cooked = CookedPhysicsResourcePayload {
      .data = std::move(source_bytes_result.value()),
      .format = format,
      .alignment = 16,
      .content_hash = 0,
    },
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
      "Physics resource descriptor processing failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto emit_start = std::chrono::steady_clock::now();
  const auto emitted_index = session.PhysicsResourceEmitter().Emit(
    std::move(result.cooked), target->virtual_path);
  const auto descriptor
    = session.PhysicsResourceEmitter().TryGetDescriptor(emitted_index);
  if (!descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.descriptor_missing",
      "Failed retrieving emitted physics resource descriptor");
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics resource descriptor emission failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto canonical_relpath_by_index
    = LoadCanonicalPhysicsSidecarRelpathsByIndex(session.CookedRoot());
  const auto requested_relpath = NormalizeRelPath(target->relpath);
  const auto existing = canonical_relpath_by_index.find(emitted_index);
  if (existing != canonical_relpath_by_index.end()
    && existing->second != requested_relpath) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.dedup_virtual_path_conflict",
      "Equivalent physics resources deduped to one resource index must share "
      "one canonical virtual_path",
      "virtual_path");
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics resource descriptor virtual path conflict");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (auto table_registry = TableRegistry(); table_registry != nullptr) {
    auto canonical_relpath = std::string {};
    if (!table_registry->TryRegisterPhysicsCanonicalDescriptorRelPath(
          session.CookedRoot(), emitted_index, requested_relpath,
          canonical_relpath)) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "physics.resource.dedup_virtual_path_conflict",
        "Equivalent physics resources deduped to one resource index must "
        "share one canonical virtual_path; canonical='"
          + canonical_relpath + "' requested='" + requested_relpath + "'",
        "virtual_path");
      ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
        "Physics resource descriptor virtual path conflict");
      co_return co_await FinalizeWithTelemetry(session);
    }
  }

  const auto full_sidecar_path
    = session.CookedRoot() / std::filesystem::path(requested_relpath);
  if (!ValidateExistingSidecarIfPresent(
        full_sidecar_path, *descriptor, session, Request(), "virtual_path")) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics resource descriptor virtual path collision");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto sidecar_emit_failed = false;
  auto sidecar_emit_error = std::string {};
  try {
    [[maybe_unused]] const auto relpath
      = session.ResourceDescriptorEmitter().EmitPhysicsResourceAtRelPath(
        requested_relpath, data::pak::core::ResourceIndexT { emitted_index },
        *descriptor);
  } catch (const std::exception& ex) {
    sidecar_emit_failed = true;
    sidecar_emit_error = ex.what();
  }
  if (sidecar_emit_failed) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.resource.sidecar_emit_failed",
      "Failed to emit physics resource sidecar descriptor: "
        + sidecar_emit_error);
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Physics resource descriptor sidecar emission failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto PhysicsResourceDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
