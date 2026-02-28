//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <string_view>
#include <utility>

#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;

auto IsZeroSourceKey(const data::SourceKey& source_key) noexcept -> bool
{
  return source_key.IsNil();
}

auto AddDiagnostic(pak::PakBuildResult& result,
  const pak::PakDiagnosticSeverity severity, const pak::PakBuildPhase phase,
  std::string code, std::string message, const std::filesystem::path& path = {})
  -> void
{
  const auto diagnostic = pak::PakDiagnostic {
    .severity = severity,
    .phase = phase,
    .code = std::move(code),
    .message = std::move(message),
    .asset_key = {},
    .resource_kind = {},
    .table_name = {},
    .path = path,
    .offset = {},
  };
  result.diagnostics.push_back(diagnostic);

  switch (severity) {
  case pak::PakDiagnosticSeverity::kInfo:
    ++result.summary.diagnostics_info;
    break;
  case pak::PakDiagnosticSeverity::kWarning:
    ++result.summary.diagnostics_warning;
    break;
  case pak::PakDiagnosticSeverity::kError:
    ++result.summary.diagnostics_error;
    break;
  }
}

auto AddDiagnosticRecord(
  pak::PakBuildResult& result, const pak::PakDiagnostic& diagnostic) -> void
{
  result.diagnostics.push_back(diagnostic);

  switch (diagnostic.severity) {
  case pak::PakDiagnosticSeverity::kInfo:
    ++result.summary.diagnostics_info;
    break;
  case pak::PakDiagnosticSeverity::kWarning:
    ++result.summary.diagnostics_warning;
    break;
  case pak::PakDiagnosticSeverity::kError:
    ++result.summary.diagnostics_error;
    break;
  }
}

auto AddRequestError(pak::PakBuildResult& result, const std::string_view code,
  const std::string_view message, const std::filesystem::path& path = {})
  -> void
{
  AddDiagnostic(result, pak::PakDiagnosticSeverity::kError,
    pak::PakBuildPhase::kRequestValidation, std::string(code),
    std::string(message), path);
}

} // namespace

namespace oxygen::content::pak {

auto PakBuilder::Build(const PakBuildRequest& request) noexcept
  -> Result<PakBuildResult>
{
  const auto build_start = std::chrono::steady_clock::now();

  PakBuildResult result {};
  result.summary.crc_computed = request.options.compute_crc32;

  if (IsZeroSourceKey(request.source_key)) {
    AddRequestError(result, "pak.request.source_key_zero",
      "PakBuildRequest::source_key must be non-zero.");
  }

  if (request.mode == BuildMode::kPatch && request.base_catalogs.empty()) {
    AddRequestError(result, "pak.request.patch_requires_base_catalogs",
      "Patch mode requires at least one base catalog.");
  }

  if (request.mode == BuildMode::kPatch
    && request.output_manifest_path.empty()) {
    AddRequestError(result, "pak.request.patch_requires_output_manifest_path",
      "Patch mode requires output_manifest_path.",
      request.output_manifest_path);
  }

  if (request.mode == BuildMode::kFull && request.options.emit_manifest_in_full
    && request.output_manifest_path.empty()) {
    AddRequestError(result,
      "pak.request.full_manifest_requires_output_manifest_path",
      "Full mode with emit_manifest_in_full=true requires "
      "output_manifest_path.",
      request.output_manifest_path);
  }

  if (result.summary.diagnostics_error > 0) {
    result.telemetry.total_duration
      = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - build_start);
    return Result<PakBuildResult>::Ok(std::move(result));
  }

  PakPlanBuilder plan_builder;
  auto plan_result = plan_builder.Build(request);

  result.telemetry.planning_duration = plan_result.planning_duration;
  result.summary.assets_processed = plan_result.summary.assets_processed;
  result.summary.resources_processed = plan_result.summary.resources_processed;
  result.summary.patch_created = plan_result.summary.patch_created;
  result.summary.patch_replaced = plan_result.summary.patch_replaced;
  result.summary.patch_deleted = plan_result.summary.patch_deleted;
  result.summary.patch_unchanged = plan_result.summary.patch_unchanged;

  for (const auto& diagnostic : plan_result.diagnostics) {
    AddDiagnosticRecord(result, diagnostic);
  }

  if (result.summary.diagnostics_error > 0 || !plan_result.plan.has_value()) {
    result.telemetry.total_duration
      = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - build_start);
    return Result<PakBuildResult>::Ok(std::move(result));
  }

  PakWriter writer;
  auto write_result = writer.Write(request, *plan_result.plan);
  result.file_size = write_result.file_size;
  result.pak_crc32 = write_result.pak_crc32;
  result.telemetry.writing_duration = write_result.writing_duration;

  for (const auto& diagnostic : write_result.diagnostics) {
    AddDiagnosticRecord(result, diagnostic);
  }

  if (request.options.fail_on_warnings && result.summary.diagnostics_warning > 0
    && result.summary.diagnostics_error == 0) {
    AddRequestError(result, "pak.request.fail_on_warnings",
      "fail_on_warnings=true and at least one warning was emitted.");
  }

  result.telemetry.total_duration
    = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - build_start);

  return Result<PakBuildResult>::Ok(std::move(result));
}

} // namespace oxygen::content::pak
