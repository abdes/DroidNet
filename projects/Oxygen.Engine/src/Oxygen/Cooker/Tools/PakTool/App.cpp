//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Cooker/Pak/PakBuildPhase.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Tools/PakTool/App.h>
#include <Oxygen/Cooker/Tools/PakTool/CliBuilder.h>
#include <Oxygen/Cooker/Tools/PakTool/CommandExecution.h>

namespace oxygen::content::pak::tool {

namespace {

  class ConsoleWriter {
  public:
    ConsoleWriter(std::ostream& out, std::ostream& err, const bool quiet,
      const bool no_color)
      : out_(out)
      , err_(err)
      , quiet_(quiet)
      , no_color_(no_color)
    {
    }

    auto Info(const std::string_view code, const std::string& message) -> void
    {
      if (quiet_) {
        return;
      }
      WriteLine(out_, "INFO", code, message, "36");
    }

    auto Report(const std::string_view code, const std::string& message) -> void
    {
      if (quiet_) {
        return;
      }
      WriteLine(out_, "REPORT", code, message, "32");
    }

    auto Warning(const std::string_view code, const std::string& message)
      -> void
    {
      if (quiet_) {
        return;
      }
      WriteLine(err_, "WARNING", code, message, "33");
    }

    auto Error(const std::string_view code, const std::string& message) -> void
    {
      WriteLine(err_, "ERROR", code, message, "31");
    }

  private:
    auto WriteLine(std::ostream& stream, const std::string_view level,
      const std::string_view code, const std::string& message,
      const std::string_view color_code) const -> void
    {
      if (!no_color_) {
        stream << "\x1b[" << color_code << "m";
      }
      stream << level;
      if (!no_color_) {
        stream << "\x1b[0m";
      }
      stream << " [" << code << "]: " << message << '\n';
    }

    std::ostream& out_;
    std::ostream& err_;
    bool quiet_ = false;
    bool no_color_ = false;
  };

  [[nodiscard]] auto CommandLineToString(const std::span<char*> argv)
    -> std::string
  {
    auto out = std::ostringstream {};
    for (size_t i = 0; i < argv.size(); ++i) {
      if (i > 0U) {
        out << ' ';
      }
      out << argv[i];
    }
    return out.str();
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
        << " base_catalogs=" << options.patch.base_catalogs.size();
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
    result_summary << "build="
                   << (result.exit_code == PakToolExitCode::kSuccess ? "success"
                          : result.exit_code == PakToolExitCode::kBuildFailure
                          ? "failed"
                          : "runtime_failure")
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

} // namespace

auto RunPakToolApp(std::span<char*> argv, std::ostream& out, std::ostream& err,
  IRequestPreparationFileSystem& prep_fs, IArtifactFileSystem& artifact_fs)
  -> int
{
  try {
    auto options = PakToolCliOptions {};
    const auto cli = BuildCli(options);
    const auto context = cli->Parse(
      static_cast<int>(argv.size()), const_cast<const char**>(argv.data()));

    const auto command_path = context.active_command->PathAsString();
    if (command_path == oxygen::clap::Command::VERSION
      || command_path == oxygen::clap::Command::HELP
      || context.ovm.HasOption(oxygen::clap::Command::HELP)) {
      return 0;
    }

    auto writer
      = ConsoleWriter(out, err, options.output.quiet, options.output.no_color);
    const auto mode
      = command_path == "patch" ? BuildMode::kPatch : BuildMode::kFull;

    writer.Info("paktool.command", FormatCommandSummary(mode, options));
    writer.Info("paktool.outputs", FormatOutputSummary(mode, options));

    auto result
      = ExecutePakToolCommand(mode, command_path, CommandLineToString(argv),
        OXYGEN_PAKTOOL_VERSION, options, prep_fs, artifact_fs);

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
    return static_cast<int>(result.exit_code);
  } catch (const oxygen::clap::CmdLineArgumentsError& ex) {
    auto writer = ConsoleWriter(out, err, false, true);
    LOG_F(ERROR, "PakTool CLI parse failed [paktool.cli.parse_failed]: {}",
      ex.what());
    writer.Error("paktool.cli.parse_failed",
      std::string("[RequestValidation] ") + ex.what());
    return static_cast<int>(PakToolExitCode::kUsageError);
  } catch (const std::exception& ex) {
    auto writer = ConsoleWriter(out, err, false, true);
    LOG_F(ERROR,
      "PakTool unhandled exception [paktool.runtime.unhandled_exception]: {}",
      ex.what());
    writer.Error("paktool.runtime.unhandled_exception",
      std::string("[Finalize] ") + ex.what());
    return static_cast<int>(PakToolExitCode::kRuntimeFailure);
  } catch (...) {
    auto writer = ConsoleWriter(out, err, false, true);
    LOG_F(ERROR,
      "PakTool unhandled exception [paktool.runtime.unknown_exception]: "
      "unknown exception");
    writer.Error(
      "paktool.runtime.unknown_exception", "[Finalize] unknown exception");
    return static_cast<int>(PakToolExitCode::kRuntimeFailure);
  }
}

} // namespace oxygen::content::pak::tool
