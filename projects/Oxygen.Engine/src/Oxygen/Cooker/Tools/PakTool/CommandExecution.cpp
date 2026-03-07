//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Cooker/Tools/PakTool/CommandExecution.h>
#include <Oxygen/Cooker/Tools/PakTool/ScriptSealing.h>

namespace oxygen::content::pak::tool {

namespace {

  [[nodiscard]] auto HasBuildErrors(const pak::PakBuildResult& result) -> bool
  {
    return result.summary.diagnostics_error > 0;
  }

  [[nodiscard]] auto PathForLog(const std::filesystem::path& path)
    -> std::string
  {
    return path.empty() ? std::string("<none>") : path.generic_string();
  }

  auto LogInfoMessage(const std::string& message) -> void
  {
    LOG_F(INFO, "{}", message);
  }

  auto LogWarningMessage(const std::string& message) -> void
  {
    LOG_F(WARNING, "{}", message);
  }

  auto LogErrorMessage(const std::string& message) -> void
  {
    LOG_F(ERROR, "{}", message);
  }

  auto LogPreparationFailure(const RequestPreparationError& error) -> void
  {
    if (error.path.empty()) {
      LogErrorMessage("PakTool request preparation failed [" + error.error_code
        + "]: " + error.error_message);
      return;
    }

    LogErrorMessage("PakTool request preparation failed [" + error.error_code
      + "]: " + error.error_message + " path='" + PathForLog(error.path) + "'");
  }

  auto LogScriptSealingFailure(const ScriptSealingError& error) -> void
  {
    auto message = std::string("PakTool script sealing failed [")
      + error.error_code + "]: " + error.error_message;
    if (!error.source_path.empty()) {
      message.append(" source='")
        .append(PathForLog(error.source_path))
        .append("'");
    }
    if (!error.descriptor_path.empty()) {
      message.append(" descriptor='")
        .append(PathForLog(error.descriptor_path))
        .append("'");
    }
    if (!error.external_source_path.empty()) {
      message.append(" external_source='")
        .append(error.external_source_path)
        .append("'");
    }
    if (!error.resolved_path.empty()) {
      message.append(" resolved='")
        .append(PathForLog(error.resolved_path))
        .append("'");
    }
    LogErrorMessage(message);
  }

  auto LogBuildDiagnostic(const pak::PakDiagnostic& diagnostic) -> void
  {
    auto message = std::string("PakTool diagnostic [") + diagnostic.code
      + "]: phase=" + std::string(to_string(diagnostic.phase))
      + " severity=" + std::string(to_string(diagnostic.severity))
      + " message=\"" + diagnostic.message + "\"";
    if (!diagnostic.path.empty()) {
      message.append(" path='").append(PathForLog(diagnostic.path)).append("'");
    }
    if (!diagnostic.asset_key.empty()) {
      message.append(" asset_key='").append(diagnostic.asset_key).append("'");
    }
    if (!diagnostic.resource_kind.empty()) {
      message.append(" resource_kind='")
        .append(diagnostic.resource_kind)
        .append("'");
    }
    if (!diagnostic.table_name.empty()) {
      message.append(" table='").append(diagnostic.table_name).append("'");
    }

    switch (diagnostic.severity) {
    case pak::PakDiagnosticSeverity::kInfo:
      break;
    case pak::PakDiagnosticSeverity::kWarning:
      LogWarningMessage(message);
      break;
    case pak::PakDiagnosticSeverity::kError:
      LogErrorMessage(message);
      break;
    }
  }

  auto LogBuildDiagnostics(const pak::PakBuildResult& result) -> void
  {
    for (const auto& diagnostic : result.diagnostics) {
      LogBuildDiagnostic(diagnostic);
    }
  }

  auto LogPublicationResult(const ArtifactPublicationResult& publication_result)
    -> void
  {
    auto message = std::string("PakTool publication result: pak=")
      + (publication_result.pak.published ? "published" : "skipped")
      + " catalog="
      + (publication_result.catalog.published ? "published" : "skipped");

    if (publication_result.manifest.has_value()) {
      message.append(" manifest=")
        .append(
          publication_result.manifest->published ? "published" : "skipped");
    } else {
      message.append(" manifest=not_requested");
    }

    if (publication_result.report.has_value()) {
      message.append(" report=")
        .append(
          publication_result.report->published ? "written" : "not_written");
    } else {
      message.append(" report=not_requested");
    }

    if (publication_result.publish_duration.has_value()) {
      message.append(" duration_us=")
        .append(std::to_string(publication_result.publish_duration->count()));
    }

    LogInfoMessage(message);
  }

  auto MarkReportIntent(ArtifactPublicationResult& publication_result,
    const ArtifactPublicationPlan& publication_plan) -> void
  {
    if (!publication_plan.report.has_value()) {
      return;
    }

    publication_result.report = ArtifactPublicationState {
      .final_path = publication_plan.report->final_path,
      .staged_path = publication_plan.report->staged_path,
      .publish_requested = true,
      .published = true,
      .cleaned_staged = true,
      .removed_stale_final = false,
      .restored_backup = false,
    };
  }

  auto EmitReport(const std::string& tool_version, const std::string& command,
    const std::string& command_line, const PakToolRequestSnapshot& snapshot,
    const ArtifactPublicationPlan& publication_plan,
    PakToolCommandResult& command_result) -> void
  {
    if (!publication_plan.report.has_value()) {
      return;
    }

    MarkReportIntent(command_result.publication_result, publication_plan);

    const auto report_input = PakToolBuildReportInput {
      .tool_version = tool_version,
      .command = command,
      .command_line = command_line,
      .request_snapshot = snapshot,
      .publication_plan = publication_plan,
      .publication_result = command_result.publication_result,
      .build_result = command_result.build_result,
      .exit_code = static_cast<int>(command_result.exit_code),
      .success = command_result.exit_code == PakToolExitCode::kSuccess,
    };

    command_result.report_write_result
      = WriteReportFile(publication_plan.report->final_path, report_input);
    if (!command_result.report_write_result.success) {
      if (command_result.publication_result.report.has_value()) {
        command_result.publication_result.report->published = false;
      }
      command_result.error_path = publication_plan.report->final_path;
      if (command_result.exit_code == PakToolExitCode::kSuccess
        || command_result.exit_code == PakToolExitCode::kBuildFailure) {
        command_result.exit_code = PakToolExitCode::kRuntimeFailure;
        command_result.error_code
          = command_result.report_write_result.error_code;
        command_result.error_message
          = command_result.report_write_result.error_message;
      }
      LogErrorMessage("PakTool report write failed ["
        + command_result.report_write_result.error_code
        + "]: " + command_result.report_write_result.error_message + " path='"
        + PathForLog(publication_plan.report->final_path) + "'");
      return;
    }

    if (command_result.publication_result.report.has_value()) {
      command_result.publication_result.report->published = true;
    }
    LogInfoMessage("PakTool diagnostics report written: path='"
      + PathForLog(publication_plan.report->final_path) + "'");
  }

  auto CleanupFailedBuildArtifacts(
    const ArtifactPublicationPlan& publication_plan,
    IArtifactFileSystem& artifact_fs,
    ArtifactPublicationResult& publication_result) -> void
  {
    auto cleanup_intent = ArtifactPublicationIntent {
      .create_parent_directories = true,
      .publish_pak = false,
      .publish_catalog = false,
      .publish_manifest = false,
      .publish_report = false,
      .suppress_stale_catalog_on_skip = true,
      .suppress_stale_manifest_on_skip = true,
    };
    publication_result
      = PublishArtifacts(publication_plan, cleanup_intent, artifact_fs);
  }

} // namespace

auto ExecutePakToolCommand(const pak::BuildMode mode, std::string command,
  std::string command_line, std::string tool_version,
  const PakToolCliOptions& options, IRequestPreparationFileSystem& prep_fs,
  IArtifactFileSystem& artifact_fs) -> PakToolCommandResult
{
  auto command_result = PakToolCommandResult {};
  LogInfoMessage("PakTool command starting: command='" + command + "' mode='"
    + std::string(to_string(mode)) + "' line='" + command_line + "'");

  const auto prepared = PreparePakToolRequest(mode, options, prep_fs);
  if (!prepared.has_value()) {
    LogPreparationFailure(prepared.error());
    command_result.exit_code = PakToolExitCode::kPreparationFailure;
    command_result.error_code = prepared.error().error_code;
    command_result.error_message = prepared.error().error_message;
    command_result.error_path = prepared.error().path;
    return command_result;
  }

  const auto& request = prepared.value();
  command_result.publication_plan = request.publication_plan;
  LogInfoMessage("PakTool request prepared: sources="
    + std::to_string(request.build_request.sources.size()) + " base_catalogs="
    + std::to_string(request.build_request.base_catalogs.size())
    + " output_pak='" + PathForLog(request.build_request.output_pak_path)
    + "' output_catalog='"
    + PathForLog(request.publication_plan.catalog.staged_path) + "'");

  const auto sealing_parent
    = request.publication_plan.pak.final_path.has_parent_path()
    ? request.publication_plan.pak.final_path.parent_path()
    : std::filesystem::temp_directory_path();
  const auto sealed_request
    = SealLooseCookedSourcesForPakBuild(request.build_request, sealing_parent);
  if (!sealed_request.has_value()) {
    LogScriptSealingFailure(sealed_request.error());
    command_result.exit_code = PakToolExitCode::kPreparationFailure;
    command_result.error_code = sealed_request.error().error_code;
    command_result.error_message = sealed_request.error().error_message;
    command_result.error_path = !sealed_request.error().descriptor_path.empty()
      ? sealed_request.error().descriptor_path
      : sealed_request.error().source_path;
    return command_result;
  }

  struct StagedSourceCleanup final {
    std::vector<std::filesystem::path> paths;

    ~StagedSourceCleanup() { CleanupStagedLooseRoots(paths); }
  } staged_source_cleanup { .paths = sealed_request->staged_loose_roots };

  if (sealed_request->sealed_script_assets > 0U) {
    LogInfoMessage("PakTool sealed loose-cooked script assets: count="
      + std::to_string(sealed_request->sealed_script_assets) + " staged_roots="
      + std::to_string(sealed_request->staged_loose_roots.size()));
  }

  auto builder = pak::PakBuilder {};
  LogInfoMessage("PakTool invoking PakBuilder");
  const auto build_result = builder.Build(sealed_request->build_request);
  if (!build_result.has_value()) {
    CleanupFailedBuildArtifacts(
      request.publication_plan, artifact_fs, command_result.publication_result);
    command_result.exit_code = PakToolExitCode::kRuntimeFailure;
    command_result.error_code = "paktool.build.runtime_failure";
    command_result.error_message = "PakBuilder returned a runtime failure.";
    LogErrorMessage("PakTool build failed [paktool.build.runtime_failure]: "
                    "PakBuilder returned a runtime failure.");
    if (!command_result.publication_result.success) {
      LogErrorMessage("PakTool cleanup after runtime build failure failed ["
        + command_result.publication_result.error_code
        + "]: " + command_result.publication_result.error_message);
    }
    EmitReport(tool_version, command, command_line, request.request_snapshot,
      request.publication_plan, command_result);
    return command_result;
  }

  command_result.build_result = build_result.value();
  LogBuildDiagnostics(command_result.build_result);
  LogInfoMessage("PakTool build completed: assets="
    + std::to_string(command_result.build_result.summary.assets_processed)
    + " resources="
    + std::to_string(command_result.build_result.summary.resources_processed)
    + " diagnostics(info="
    + std::to_string(command_result.build_result.summary.diagnostics_info)
    + ",warning="
    + std::to_string(command_result.build_result.summary.diagnostics_warning)
    + ",error="
    + std::to_string(command_result.build_result.summary.diagnostics_error)
    + ")");

  const auto staged_catalog_write
    = pak::PakCatalogIo::Write(request.publication_plan.catalog.staged_path,
      command_result.build_result.output_catalog);
  if (!staged_catalog_write.has_value()) {
    CleanupFailedBuildArtifacts(
      request.publication_plan, artifact_fs, command_result.publication_result);
    command_result.exit_code = PakToolExitCode::kRuntimeFailure;
    command_result.error_code = "paktool.catalog.write_failed";
    command_result.error_message
      = "Failed to persist the staged pak catalog sidecar.";
    command_result.error_path = request.publication_plan.catalog.staged_path;
    LogErrorMessage("PakTool staged catalog write failed "
                    "[paktool.catalog.write_failed]: Failed to persist the "
                    "staged pak catalog "
                    "sidecar. path='"
      + PathForLog(request.publication_plan.catalog.staged_path) + "'");
    if (!command_result.publication_result.success) {
      LogErrorMessage(
        "PakTool cleanup after staged catalog write failure failed ["
        + command_result.publication_result.error_code
        + "]: " + command_result.publication_result.error_message);
    }
    EmitReport(tool_version, command, command_line, request.request_snapshot,
      request.publication_plan, command_result);
    return command_result;
  }
  LogInfoMessage("PakTool staged catalog persisted: path='"
    + PathForLog(request.publication_plan.catalog.staged_path) + "'");

  const auto build_success = !HasBuildErrors(command_result.build_result);
  auto publish_intent = ArtifactPublicationIntent {
    .create_parent_directories = true,
    .publish_pak = build_success,
    .publish_catalog = build_success,
    .publish_manifest = build_success
      && request.publication_plan.manifest.has_value()
      && command_result.build_result.patch_manifest.has_value(),
    .publish_report = false,
    .suppress_stale_catalog_on_skip = !build_success,
    .suppress_stale_manifest_on_skip = !build_success,
  };

  command_result.publication_result
    = PublishArtifacts(request.publication_plan, publish_intent, artifact_fs);
  if (!command_result.publication_result.success) {
    command_result.exit_code = PakToolExitCode::kRuntimeFailure;
    command_result.error_code = command_result.publication_result.error_code;
    command_result.error_message
      = command_result.publication_result.error_message;
    LogErrorMessage("PakTool artifact publication failed ["
      + command_result.publication_result.error_code
      + "]: " + command_result.publication_result.error_message);
    EmitReport(tool_version, command, command_line, request.request_snapshot,
      request.publication_plan, command_result);
    return command_result;
  }
  LogPublicationResult(command_result.publication_result);

  command_result.exit_code = build_success ? PakToolExitCode::kSuccess
                                           : PakToolExitCode::kBuildFailure;
  EmitReport(tool_version, command, command_line, request.request_snapshot,
    request.publication_plan, command_result);
  LogInfoMessage("PakTool command finished: exit_code="
    + std::to_string(static_cast<int>(command_result.exit_code)));
  return command_result;
}

} // namespace oxygen::content::pak::tool
