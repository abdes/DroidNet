//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <iomanip>
#include <ios>
#include <memory>
#include <optional>
#include <ostream>
#include <ratio>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#include "ArtifactPublication.h"
#include "PakToolOptions.h"
#include "RequestPreparation.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Cooker/Pak/PakBuildPhase.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/PakTool/CliBuilder.h>
#include <Oxygen/Cooker/Tools/PakTool/CommandExecution.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolCommands.h>
#include <Oxygen/Data/CookedSource.h>

#ifndef OXYGEN_PAKTOOL_VERSION
#  error OXYGEN_PAKTOOL_VERSION must be defined for PakTool CLI version.
#endif

namespace oxygen::content::pak::tool {

namespace toolkit = oxygen::content::toolkit;

namespace {

  //! How the console writer presents its lines.
  struct ConsoleStyle {
    //! Only warnings and errors are written.
    bool quiet = false;
    //! Levels are written without ANSI colors.
    bool no_color = false;
  };

  //! A line's severity, as written, and its ANSI color.
  struct LineLevel {
    std::string_view name;
    std::string_view color;
  };

  constexpr auto kInfoLevel = LineLevel { .name = "INFO", .color = "36" };
  constexpr auto kReportLevel = LineLevel { .name = "REPORT", .color = "32" };
  constexpr auto kWarningLevel = LineLevel { .name = "WARNING", .color = "33" };
  constexpr auto kErrorLevel = LineLevel { .name = "ERROR", .color = "31" };

  class ConsoleWriter {
  public:
    ConsoleWriter(const toolkit::ToolStreams streams, const ConsoleStyle style)
      : streams_(streams)
      , style_(style)
    {
    }

    auto Info(const std::string_view code, const std::string& message) -> void
    {
      if (!style_.quiet) {
        WriteLine(*streams_.out, kInfoLevel, code, message);
      }
    }

    auto Report(const std::string_view code, const std::string& message) -> void
    {
      if (!style_.quiet) {
        WriteLine(*streams_.out, kReportLevel, code, message);
      }
    }

    auto Warning(const std::string_view code, const std::string& message)
      -> void
    {
      if (!style_.quiet) {
        WriteLine(*streams_.err, kWarningLevel, code, message);
      }
    }

    auto Error(const std::string_view code, const std::string& message) -> void
    {
      WriteLine(*streams_.err, kErrorLevel, code, message);
    }

  private:
    auto WriteLine(std::ostream& stream, const LineLevel& level,
      const std::string_view code, const std::string& message) const -> void
    {
      if (style_.no_color) {
        stream << level.name;
      } else {
        stream << "\x1b[" << level.color << "m" << level.name << "\x1b[0m";
      }
      stream << " [" << code << "]: " << message << '\n';
    }

    toolkit::ToolStreams streams_;
    ConsoleStyle style_;
  };

  //! How a build ended, as the result summary names it.
  [[nodiscard]] auto BuildOutcomeName(const PakToolExitCode exit_code)
    -> std::string_view
  {
    switch (exit_code) {
    case PakToolExitCode::kSuccess:
      return "success";
    case PakToolExitCode::kBuildFailure:
      return "failed";
    case PakToolExitCode::kUsageError:
    case PakToolExitCode::kPreparationFailure:
    case PakToolExitCode::kRuntimeFailure:
      break;
    }
    return "runtime_failure";
  }

  [[nodiscard]] auto CountSources(const PakToolCliOptions& options,
    const data::CookedSourceKind kind) -> size_t
  {
    auto count = size_t { 0U };
    for (const auto& source : options.request.sources) {
      if (source.kind == kind) {
        ++count;
      }
    }
    return count;
  }

  [[nodiscard]] auto FormatMillis(
    const std::optional<std::chrono::microseconds>& duration) -> std::string
  {
    if (!duration.has_value()) {
      return "n/a";
    }

    auto out = std::ostringstream {};
    out << std::fixed << std::setprecision(3)
        << std::chrono::duration<double, std::milli>(*duration).count();
    return out.str();
  }

  [[nodiscard]] auto FormatCommandSummary(
    const BuildMode mode, const PakToolCliOptions& options) -> std::string
  {
    auto out = std::ostringstream {};
    out << "mode=" << to_string(mode) << " loose_sources="
        << CountSources(options, data::CookedSourceKind::kLooseCooked)
        << " pak_sources="
        << CountSources(options, data::CookedSourceKind::kPak)
        << " base_catalogs=" << options.patch.base_paks.size()
        << " dependency_catalogs=" << options.request.dependency_paks.size();
    return out.str();
  }

  [[nodiscard]] auto FormatOutputSummary(
    const BuildMode mode, const PakToolCliOptions& options) -> std::string
  {
    auto out = std::ostringstream {};
    out << "pak='" << options.request.output_pak.string() << "'"
        << " catalog='" << options.request.catalog_output.string() << "'";
    const auto manifest = mode == BuildMode::kPatch
      ? options.patch.manifest_output
      : options.build.manifest_output;
    if (!manifest.empty()) {
      out << " manifest='" << manifest.string() << "'";
    }
    if (!options.output.diagnostics_file.empty()) {
      out << " report='" << options.output.diagnostics_file.string() << "'";
    }
    return out.str();
  }

  [[nodiscard]] auto PublicationStateToString(
    const ArtifactPublicationState& state, const bool is_report)
    -> std::string_view
  {
    if (state.publish_requested) {
      if (state.published) {
        return is_report ? "written" : "published";
      }
      return "failed";
    }
    if (state.removed_stale_final) {
      return "suppressed";
    }
    return "skipped";
  }

  [[nodiscard]] auto FormatPublicationSummary(
    const ArtifactPublicationResult& publication_result) -> std::string
  {
    auto out = std::ostringstream {};
    out << "pak=" << PublicationStateToString(publication_result.pak, false)
        << " catalog="
        << PublicationStateToString(publication_result.catalog, false)
        << " manifest=";
    if (publication_result.manifest.has_value()) {
      out << PublicationStateToString(*publication_result.manifest, false);
    } else {
      out << "not_requested";
    }
    out << " report=";
    if (publication_result.report.has_value()) {
      out << PublicationStateToString(*publication_result.report, true);
    } else {
      out << "not_requested";
    }
    return out.str();
  }

  [[nodiscard]] auto FormatSummary(const pak::PakBuildSummary& summary)
    -> std::string
  {
    auto out = std::ostringstream {};
    out << "info=" << summary.diagnostics_info
        << " warning=" << summary.diagnostics_warning
        << " error=" << summary.diagnostics_error
        << " assets=" << summary.assets_processed
        << " resources=" << summary.resources_processed
        << " patch_created=" << summary.patch_created
        << " patch_replaced=" << summary.patch_replaced
        << " patch_deleted=" << summary.patch_deleted
        << " patch_unchanged=" << summary.patch_unchanged;
    return out.str();
  }

  [[nodiscard]] auto FormatTelemetry(const PakToolCommandResult& result)
    -> std::string
  {
    auto out = std::ostringstream {};
    out << "planning="
        << FormatMillis(result.build_result.telemetry.planning_duration)
        << " writing="
        << FormatMillis(result.build_result.telemetry.writing_duration)
        << " manifest="
        << FormatMillis(result.build_result.telemetry.manifest_duration)
        << " publish="
        << FormatMillis(result.publication_result.publish_duration) << " total="
        << FormatMillis(result.build_result.telemetry.total_duration);
    return out.str();
  }

  auto EmitDiagnostic(
    const pak::PakDiagnostic& diagnostic, ConsoleWriter& writer) -> void
  {
    auto message = std::ostringstream {};
    message << "[" << to_string(diagnostic.phase) << "] " << diagnostic.message;
    if (!diagnostic.path.empty()) {
      message << " path='" << diagnostic.path.string() << "'";
    }

    switch (diagnostic.severity) {
    case pak::PakDiagnosticSeverity::kInfo:
      break;
    case pak::PakDiagnosticSeverity::kWarning:
      writer.Warning(diagnostic.code, message.str());
      break;
    case pak::PakDiagnosticSeverity::kError:
      writer.Error(diagnostic.code, message.str());
      break;
    }
  }

  auto EmitFinalConsoleSummary(const PakToolCliOptions& options,
    const PakToolCommandResult& result, ConsoleWriter& writer) -> void
  {
    for (const auto& diagnostic : result.build_result.diagnostics) {
      EmitDiagnostic(diagnostic, writer);
    }

    writer.Report(
      "paktool.summary", FormatSummary(result.build_result.summary));
    writer.Report("paktool.timing_ms", FormatTelemetry(result));
    writer.Report("paktool.publication",
      FormatPublicationSummary(result.publication_result));

    auto result_summary = std::ostringstream {};
    result_summary << "build=" << BuildOutcomeName(result.exit_code)
                   << " exit_code=" << static_cast<int>(result.exit_code);
    if (options.output.diagnostics_file.empty()) {
      result_summary << " report=not_requested";
    } else if (result.report_write_result.success) {
      result_summary << " report=written";
    } else {
      result_summary << " report=failed";
    }
    writer.Report("paktool.result", result_summary.str());
  }

  auto ReportUnhandledException(ConsoleWriter& writer, const std::exception& ex)
    -> int
  {
    LOG_F(ERROR,
      "PakTool unhandled exception [paktool.runtime.unhandled_exception]: {}",
      ex.what());
    writer.Error("paktool.runtime.unhandled_exception",
      std::string("[Finalize] ") + ex.what());
    return static_cast<int>(PakToolExitCode::kRuntimeFailure);
  }

  auto ReportCommandLineFailure(const oxygen::clap::CmdLineArgumentsError& ex,
    const toolkit::ToolStreams streams) -> int
  {
    auto writer = ConsoleWriter(streams, { .no_color = true });
    LOG_F(ERROR, "PakTool CLI parse failed [paktool.cli.parse_failed]: {}",
      ex.what());
    writer.Error("paktool.cli.parse_failed",
      std::string("[RequestValidation] ") + ex.what());
    return static_cast<int>(PakToolExitCode::kUsageError);
  }

} // namespace

auto PakToolInfo() -> toolkit::ToolInfo
{
  return toolkit::ToolInfo {
    .program = "Oxygen.Cooker.PakTool",
    .version = OXYGEN_PAKTOOL_VERSION,
    .about = "Build and publish Oxygen pak archives and their sidecar "
             "artifacts.",
    .on_parse_error = ReportCommandLineFailure,
  };
}

PakToolCommand::PakToolCommand(const pak::BuildMode mode,
  PakToolCliOptions& options, IRequestPreparationFileSystem& prep_fs,
  IArtifactFileSystem& artifact_fs)
  : mode_(mode)
  , options_(&options)
  , prep_fs_(&prep_fs)
  , artifact_fs_(&artifact_fs)
{
}

auto PakToolCommand::Name() const -> std::string_view
{
  return mode_ == BuildMode::kPatch ? "patch" : "build";
}

auto PakToolCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  return mode_ == BuildMode::kPatch ? BuildPatchCommand(*options_)
                                    : BuildBuildCommand(*options_);
}

auto PakToolCommand::Run(toolkit::ToolContext& context) -> toolkit::ToolResult
{
  const auto& options = *options_;
  try {
    auto writer = ConsoleWriter(context.Streams(),
      {
        .quiet = options.output.quiet,
        .no_color = options.output.no_color,
      });

    writer.Info("paktool.command", FormatCommandSummary(mode_, options));
    writer.Info("paktool.outputs", FormatOutputSummary(mode_, options));

    auto result
      = ExecutePakToolCommand(mode_, std::string(Name()), context.CommandLine(),
        OXYGEN_PAKTOOL_VERSION, options, *prep_fs_, *artifact_fs_);

    if (!result.error_code.empty()) {
      const auto phase_code
        = result.exit_code == PakToolExitCode::kPreparationFailure
        ? std::string_view { "RequestValidation" }
        : std::string_view { "Finalize" };
      auto message = std::ostringstream {};
      message << "[" << phase_code << "] " << result.error_message;
      if (!result.error_path.empty()) {
        message << " path='" << result.error_path.string() << "'";
      }
      writer.Error(result.error_code, message.str());
    }

    EmitFinalConsoleSummary(options, result, writer);
    if (result.exit_code == PakToolExitCode::kSuccess) {
      return {};
    }
    return std::unexpected(
      toolkit::ToolError::WithCode(static_cast<int>(result.exit_code)));
  } catch (const std::exception& ex) {
    auto writer = ConsoleWriter(context.Streams(), { .no_color = true });
    return std::unexpected(
      toolkit::ToolError::WithCode(ReportUnhandledException(writer, ex)));
  } catch (...) {
    auto writer = ConsoleWriter(context.Streams(), { .no_color = true });
    LOG_F(ERROR,
      "PakTool unhandled exception [paktool.runtime.unknown_exception]: "
      "unknown exception");
    writer.Error(
      "paktool.runtime.unknown_exception", "[Finalize] unknown exception");
    return std::unexpected(toolkit::ToolError::WithCode(
      static_cast<int>(PakToolExitCode::kRuntimeFailure)));
  }
}

auto RunPakToolApp(std::span<char*> argv, std::ostream& out, std::ostream& err,
  IRequestPreparationFileSystem& prep_fs, IArtifactFileSystem& artifact_fs)
  -> int
{
  auto options = PakToolCliOptions {};
  auto build = PakToolCommand(BuildMode::kFull, options, prep_fs, artifact_fs);
  auto patch = PakToolCommand(BuildMode::kPatch, options, prep_fs, artifact_fs);
  auto commands = std::array<toolkit::ToolCommand*, 2> { &build, &patch };

  return toolkit::RunTool(PakToolInfo(), commands,
    std::span<const char* const>(argv),
    toolkit::ToolStreams {
      .out = observer_ptr<std::ostream> { &out },
      .err = observer_ptr<std::ostream> { &err },
    });
}

} // namespace oxygen::content::pak::tool
