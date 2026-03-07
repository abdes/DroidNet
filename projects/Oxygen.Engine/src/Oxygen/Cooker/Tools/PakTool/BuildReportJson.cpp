//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <fstream>
#include <span>
#include <sstream>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/SourceKey.h>

#include <Oxygen/Cooker/Tools/PakTool/BuildReportJson.h>

namespace oxygen::content::pak::tool {

namespace {

  constexpr auto kToolName = std::string_view { "Oxygen.Cooker.PakTool" };
  constexpr auto kBuildReportSchemaRef = std::string_view {
    "https://oxygen-engine.dev/schemas/oxygen.pak-build-report.schema.json"
  };

  template <size_t N>
  [[nodiscard]] auto IsAllZero(const std::array<uint8_t, N>& bytes) -> bool
  {
    for (const auto byte : bytes) {
      if (byte != 0U) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto ToHex(const std::span<const uint8_t> bytes) -> std::string
  {
    constexpr auto kHex = std::string_view { "0123456789abcdef" };
    auto out = std::string {};
    out.resize(bytes.size() * 2U);

    for (size_t index = 0; index < bytes.size(); ++index) {
      const auto value = bytes[index];
      out[index * 2U] = kHex[(value >> 4U) & 0x0FU];
      out[index * 2U + 1U] = kHex[value & 0x0FU];
    }

    return out;
  }

  [[nodiscard]] auto ToMillis(
    const std::optional<std::chrono::microseconds>& duration) -> ordered_json
  {
    if (!duration.has_value()) {
      return nullptr;
    }
    return std::chrono::duration<double, std::milli>(*duration).count();
  }

  [[nodiscard]] auto ModeToString(const BuildMode mode) -> std::string_view
  {
    switch (mode) {
    case BuildMode::kFull:
      return "full";
    case BuildMode::kPatch:
      return "patch";
    }
    return "full";
  }

  [[nodiscard]] auto SourceKindToString(const data::CookedSourceKind kind)
    -> std::string_view
  {
    switch (kind) {
    case data::CookedSourceKind::kLooseCooked:
      return "loose_cooked";
    case data::CookedSourceKind::kPak:
      return "pak";
    }
    return "loose_cooked";
  }

  [[nodiscard]] auto SeverityToString(const PakDiagnosticSeverity severity)
    -> std::string_view
  {
    switch (severity) {
    case PakDiagnosticSeverity::kInfo:
      return "info";
    case PakDiagnosticSeverity::kWarning:
      return "warning";
    case PakDiagnosticSeverity::kError:
      return "error";
    }
    return "info";
  }

  [[nodiscard]] auto PhaseToString(const PakBuildPhase phase)
    -> std::string_view
  {
    switch (phase) {
    case PakBuildPhase::kRequestValidation:
      return "request_validation";
    case PakBuildPhase::kPlanning:
      return "planning";
    case PakBuildPhase::kWriting:
      return "writing";
    case PakBuildPhase::kManifest:
      return "manifest";
    case PakBuildPhase::kFinalize:
      return "finalize";
    }
    return "planning";
  }

  [[nodiscard]] auto CatalogIsAvailable(const data::PakCatalog& catalog) -> bool
  {
    return !catalog.source_key.IsNil() || catalog.content_version != 0U
      || !catalog.entries.empty() || !IsAllZero(catalog.catalog_digest);
  }

  [[nodiscard]] auto PakWriteMetadataAvailable(
    const PakToolBuildReportInput& input) -> bool
  {
    return input.build_result.telemetry.writing_duration.has_value()
      || input.publication_result.pak.published;
  }

  [[nodiscard]] auto BuildRequestJson(const PakToolRequestSnapshot& snapshot)
    -> ordered_json
  {
    auto sources = ordered_json::array();
    for (const auto& source : snapshot.request.sources) {
      sources.push_back({
        { "kind", std::string(SourceKindToString(source.kind)) },
        { "path", source.path.string() },
      });
    }

    auto base_catalogs = ordered_json::array();
    for (const auto& path : snapshot.base_catalog_paths) {
      base_catalogs.push_back(path.string());
    }

    return ordered_json {
      { "mode", std::string(ModeToString(snapshot.request.mode)) },
      { "source_key", data::to_string(snapshot.request.source_key) },
      { "content_version", snapshot.request.content_version },
      { "sources", std::move(sources) },
      { "base_catalogs", std::move(base_catalogs) },
      { "options",
        ordered_json {
          { "deterministic", snapshot.request.options.deterministic },
          { "embed_browse_index", snapshot.request.options.embed_browse_index },
          { "compute_crc32", snapshot.request.options.compute_crc32 },
          { "fail_on_warnings", snapshot.request.options.fail_on_warnings },
          { "emit_manifest_in_full",
            snapshot.request.options.emit_manifest_in_full },
        } },
      { "patch_compatibility",
        ordered_json {
          { "require_exact_base_set",
            snapshot.request.patch_compat.require_exact_base_set },
          { "require_content_version_match",
            snapshot.request.patch_compat.require_content_version_match },
          { "require_base_source_key_match",
            snapshot.request.patch_compat.require_base_source_key_match },
          { "require_catalog_digest_match",
            snapshot.request.patch_compat.require_catalog_digest_match },
        } },
    };
  }

  [[nodiscard]] auto BuildArtifactsJson(const PakToolBuildReportInput& input)
    -> ordered_json
  {
    const auto catalog_available
      = CatalogIsAvailable(input.build_result.output_catalog);
    const auto pak_metadata_available = PakWriteMetadataAvailable(input);
    const auto manifest_requested = input.publication_plan.manifest.has_value();
    const auto manifest_published
      = input.publication_result.manifest.has_value()
      && input.publication_result.manifest->published;
    const auto manifest_emitted = input.build_result.patch_manifest.has_value();
    const auto report_final_path = input.publication_plan.report.has_value()
      ? input.publication_plan.report->final_path.string()
      : std::string {};
    const auto report_staged_path = input.publication_plan.report.has_value()
      ? ordered_json(input.publication_plan.report->staged_path.string())
      : ordered_json(nullptr);
    const auto report_published = input.publication_result.report.has_value()
      && input.publication_result.report->published;

    return ordered_json {
      { "pak",
        ordered_json {
          { "final_path", input.publication_plan.pak.final_path.string() },
          { "staged_path", input.publication_plan.pak.staged_path.string() },
          { "published", input.publication_result.pak.published },
          { "size_bytes",
            pak_metadata_available ? ordered_json(input.build_result.file_size)
                                   : ordered_json(nullptr) },
          { "crc32",
            pak_metadata_available
                && input.request_snapshot.request.options.compute_crc32
              ? ordered_json(input.build_result.pak_crc32)
              : ordered_json(nullptr) },
        } },
      { "catalog",
        ordered_json {
          { "final_path", input.publication_plan.catalog.final_path.string() },
          { "staged_path",
            input.publication_plan.catalog.staged_path.string() },
          { "published", input.publication_result.catalog.published },
          { "catalog_digest",
            catalog_available
              ? ordered_json(ToHex(std::span {
                  input.build_result.output_catalog.catalog_digest }))
              : ordered_json(nullptr) },
        } },
      { "manifest",
        ordered_json {
          { "requested", manifest_requested },
          { "emitted", manifest_emitted },
          { "final_path",
            manifest_requested
              ? ordered_json(
                  input.publication_plan.manifest->final_path.string())
              : ordered_json(nullptr) },
          { "staged_path",
            manifest_requested
              ? ordered_json(
                  input.publication_plan.manifest->staged_path.string())
              : ordered_json(nullptr) },
          { "published", manifest_published },
        } },
      { "report",
        ordered_json {
          { "final_path", report_final_path },
          { "staged_path", report_staged_path },
          { "published", report_published },
        } },
    };
  }

  [[nodiscard]] auto BuildSummaryJson(const PakBuildSummary& summary)
    -> ordered_json
  {
    return ordered_json {
      { "diagnostics_info", summary.diagnostics_info },
      { "diagnostics_warning", summary.diagnostics_warning },
      { "diagnostics_error", summary.diagnostics_error },
      { "assets_processed", summary.assets_processed },
      { "resources_processed", summary.resources_processed },
      { "patch_created", summary.patch_created },
      { "patch_replaced", summary.patch_replaced },
      { "patch_deleted", summary.patch_deleted },
      { "patch_unchanged", summary.patch_unchanged },
      { "crc_computed", summary.crc_computed },
    };
  }

  [[nodiscard]] auto BuildTelemetryJson(const PakToolBuildReportInput& input)
    -> ordered_json
  {
    return ordered_json {
      { "time_ms_planning",
        ToMillis(input.build_result.telemetry.planning_duration) },
      { "time_ms_writing",
        ToMillis(input.build_result.telemetry.writing_duration) },
      { "time_ms_manifest",
        ToMillis(input.build_result.telemetry.manifest_duration) },
      { "time_ms_publish",
        ToMillis(input.publication_result.publish_duration) },
      { "time_ms_total",
        ToMillis(input.build_result.telemetry.total_duration) },
    };
  }

  [[nodiscard]] auto BuildDiagnosticsJson(
    const std::vector<PakDiagnostic>& diagnostics) -> ordered_json
  {
    auto json = ordered_json::array();
    for (const auto& diagnostic : diagnostics) {
      auto entry = ordered_json {
        { "severity", std::string(SeverityToString(diagnostic.severity)) },
        { "phase", std::string(PhaseToString(diagnostic.phase)) },
        { "code", diagnostic.code },
        { "message", diagnostic.message },
      };
      if (!diagnostic.asset_key.empty()) {
        entry["asset_key"] = diagnostic.asset_key;
      }
      if (!diagnostic.resource_kind.empty()) {
        entry["resource_kind"] = diagnostic.resource_kind;
      }
      if (!diagnostic.table_name.empty()) {
        entry["table_name"] = diagnostic.table_name;
      }
      if (!diagnostic.path.empty()) {
        entry["path"] = diagnostic.path.string();
      }
      if (diagnostic.offset.has_value()) {
        entry["offset"] = *diagnostic.offset;
      }
      json.push_back(std::move(entry));
    }
    return json;
  }

} // namespace

auto ToReportJson(const PakToolBuildReportInput& input) -> ordered_json
{
  return ordered_json {
    { "$schema", std::string(kBuildReportSchemaRef) },
    { "schema_version", 1 },
    { "tool_name", std::string(kToolName) },
    { "tool_version", input.tool_version },
    { "command", input.command },
    { "command_line", input.command_line },
    { "request", BuildRequestJson(input.request_snapshot) },
    { "artifacts", BuildArtifactsJson(input) },
    { "summary", BuildSummaryJson(input.build_result.summary) },
    { "telemetry", BuildTelemetryJson(input) },
    { "diagnostics", BuildDiagnosticsJson(input.build_result.diagnostics) },
    { "exit_code", input.exit_code },
    { "success", input.success },
  };
}

auto ToCanonicalJsonString(const PakToolBuildReportInput& input) -> std::string
{
  auto document = ToReportJson(input);
  auto text = document.dump(2);
  text.push_back('\n');
  return text;
}

auto WriteReportFile(const std::filesystem::path& output_path,
  const PakToolBuildReportInput& input) -> ReportWriteResult
{
  if (output_path.empty()) {
    return ReportWriteResult {
      .success = false,
      .error_code = "paktool.report.output_path_empty",
      .error_message = "Report emission requires a non-empty output path.",
    };
  }

  const auto parent = output_path.parent_path();
  if (!parent.empty()) {
    auto ec = std::error_code {};
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      return ReportWriteResult {
        .success = false,
        .error_code = "paktool.report.create_parent_directory_failed",
        .error_message = "Failed to create report parent directory.",
      };
    }
  }

  auto output = std::ofstream(output_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return ReportWriteResult {
      .success = false,
      .error_code = "paktool.report.write_open_failed",
      .error_message = "Failed to open report output path for writing.",
    };
  }

  const auto payload = ToCanonicalJsonString(input);
  output << payload;
  output.flush();
  if (!output.good()) {
    return ReportWriteResult {
      .success = false,
      .error_code = "paktool.report.write_failed",
      .error_message = "Failed to write report payload.",
    };
  }

  return ReportWriteResult { .success = true };
}

} // namespace oxygen::content::pak::tool
