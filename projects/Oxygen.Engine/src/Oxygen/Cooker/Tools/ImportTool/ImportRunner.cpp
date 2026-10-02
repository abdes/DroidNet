//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ratio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/OptionValuesMap.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/RetainedModelImport.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ReportJson.h>
#include <Oxygen/Cooker/Tools/ImportTool/UI/JobViewModel.h>
#include <Oxygen/Cooker/Tools/ImportTool/UI/Screens/ImportScreen.h>

#ifndef OXYGEN_IMPORT_TOOL_VERSION
#  error OXYGEN_IMPORT_TOOL_VERSION must be defined for ImportTool reports.
#endif

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::ordered_json;

  auto ResolveReportPath(std::string_view report_path,
    const std::filesystem::path& cooked_root,
    const oxygen::observer_ptr<IMessageWriter>& writer)
    -> std::optional<std::filesystem::path>
  {
    std::filesystem::path path(report_path);
    if (path.is_absolute()) {
      return path;
    }
    if (cooked_root.empty()) {
      if (writer) {
        writer->Error(
          "ERROR: --report requires a cooked root when using a relative path");
      }
      return std::nullopt;
    }
    return (cooked_root / path).lexically_normal();
  }

  auto WriteJsonReport(const ordered_json& payload,
    const std::filesystem::path& report_path,
    const oxygen::observer_ptr<IMessageWriter>& writer) -> bool
  {
    std::error_code ec;
    const auto parent = report_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        if (writer) {
          writer->Error(fmt::format(
            "ERROR: failed to create report directory: {}", parent.string()));
        }
        return false;
      }
    }

    std::ofstream output(report_path);
    if (!output) {
      if (writer) {
        writer->Error(fmt::format(
          "ERROR: failed to open report file: {}", report_path.string()));
      }
      return false;
    }
    output << payload.dump(2) << "\n";
    return true;
  }

  auto ResolveReportJobType(const ImportRequest& request) -> std::string
  {
    if (request.physics.has_value()) {
      return "physics-sidecar";
    }
    if (request.input.has_value()) {
      return "input";
    }
    if (request.GetFormat() != ImportFormat::kUnknown) {
      return std::string(to_string(request.GetFormat()));
    }
    switch (request.options.scripting.import_kind) {
    case ScriptingImportKind::kScriptAsset:
      return "script";
    case ScriptingImportKind::kScriptingSidecar:
      return "script-sidecar";
    case ScriptingImportKind::kNone:
      break;
    }
    return "unknown";
  }

} // namespace

auto RunImportJob(const ImportRequest& request,
  oxygen::observer_ptr<IMessageWriter> writer,
  const std::string_view report_path, const std::string_view command_line,
  const bool enable_tui, oxygen::observer_ptr<AsyncImportService> service,
  std::shared_ptr<RetainedModelImport> publication)
  -> std::expected<void, std::error_code>
{
  const auto start_time = std::chrono::steady_clock::now();
  const auto session_started = std::chrono::system_clock::now();
  DCHECK_NOTNULL_F(writer, "Message writer must be provided by main");
  DCHECK_NOTNULL_F(service, "Import service must be provided by main");
  CHECK_F(!command_line.empty(), "Command line is required for report output");

  std::mutex mutex;
  std::condition_variable cv;
  std::optional<ImportReport> report;
  JobProgressTrace progress_trace {};

  ImportReport report_copy {};
  std::optional<std::string> submit_error;
  bool have_report = false;
  std::vector<std::string> recent_logs;
  {
    const auto on_complete
      = [&](ImportJobId, const ImportReport& result) -> void {
      {
        std::scoped_lock lock(mutex);
        report = result;
        recent_logs.push_back(
          fmt::format("Job Completed: {}", result.success ? "OK" : "FAIL"));
        if (recent_logs.size() > 50) {
          recent_logs.erase(recent_logs.begin(), recent_logs.end() - 50);
        }
      }
      cv.notify_one();
    };

    const auto on_progress = [&](const ProgressEvent& progress) -> void {
      const auto now = std::chrono::steady_clock::now();
      {
        std::scoped_lock lock(mutex);
        UpdateProgressTrace(progress_trace, progress, now);

        if (progress.header.kind == ProgressEventKind::kJobStarted) {
          recent_logs.push_back("Job Started");
        } else if (progress.header.kind == ProgressEventKind::kJobFinished) {
          recent_logs.push_back("Job Finished");
        }

        if (const auto* item = GetItemProgress(progress);
          item && (!item->item_name.empty())) {
          if (progress.header.kind == ProgressEventKind::kItemStarted) {
            recent_logs.push_back(fmt::format("Started {}", item->item_name));
          } else if (progress.header.kind == ProgressEventKind::kItemFinished) {
            recent_logs.push_back(fmt::format("Finished {}", item->item_name));
          }
        }

        if (recent_logs.size() > 50) {
          recent_logs.erase(recent_logs.begin(), recent_logs.end() - 50);
        }
      }

      {
        std::string msg = fmt::format("event={} phase={} overall={}",
          to_string(progress.header.kind),
          nostd::to_string(progress.header.phase),
          progress.header.overall_progress);
        if (const auto* item = GetItemProgress(progress);
          item && (!item->item_name.empty())) {
          msg.append(fmt::format(" item={}", item->item_name));
        }

        writer->Progress(msg);
      }
    };

    LOG_F(INFO, "ImportTool submit job: source='{}' with_content_hashing={}",
      request.source_path.string(), request.options.with_content_hashing);
    const auto job_id = publication
      ? service->SubmitRetainedImport(publication, on_complete, on_progress)
      : service->SubmitImport(request, on_complete, on_progress);
    if (!job_id) {
      submit_error = "ERROR: failed to submit import job";
    } else {
      // If TUI is enabled and not quiet, run the interactive screen while job
      // runs
      if (enable_tui) {
        ImportScreen screen;
        screen.SetDataProvider([&] -> JobViewModel {
          std::scoped_lock lock(mutex);
          JobViewModel vm;
          // derive progress from phases if available
          if (!progress_trace.phases.empty()) {
            const auto& p = progress_trace.phases.back();
            vm.progress = p.items_total == 0
              ? 0.0F
              : static_cast<float>(p.items_completed)
                / static_cast<float>(p.items_total);
          } else {
            vm.progress = 0.0F;
          }
          vm.status = report.has_value()
            ? (report->success ? "Completed" : "Failed")
            : "Running";
          vm.recent_logs = recent_logs;
          if (progress_trace.started.has_value()) {
            vm.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - *progress_trace.started);
          }
          vm.completed = report.has_value();
          vm.success = report.has_value() ? report->success : false;
          if (vm.completed) {
            vm.progress = 1.0F;
          }
          return vm;
        });

        // The provided writer for TUI runs should already be muted.
        screen.Run();
      }

      std::unique_lock lock(mutex);
      while (!report.has_value()) {
        cv.wait(lock);
      }

      report_copy = *report;
      have_report = true;
    }

    service->Stop();
  }

  if (submit_error.has_value()) {
    writer->Error(*submit_error);
    return std::unexpected(
      std::make_error_code(std::errc::state_not_recoverable));
  }

  if (!have_report) {
    writer->Error("ERROR: import failed with no report");
    CHECK_F(false, "Import completed without a report");
    return std::unexpected(
      std::make_error_code(std::errc::state_not_recoverable));
  }

  std::optional<std::filesystem::path> written_report;
  if (!report_path.empty()) {
    const auto cooked_root = report_copy.cooked_root;
    const auto resolved_path
      = ResolveReportPath(report_path, cooked_root, writer);
    if (!resolved_path.has_value()) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    const auto session_ended = std::chrono::system_clock::now();
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start_time)
                              .count();
    CHECK_F(!cooked_root.empty(), "Cooked root is required in report output");

    ordered_json payload = ordered_json::object();
    payload.update({ { "report_version", std::string(kReportVersion) },
      { "session",
        {
          { "id", MakeSessionId(session_started) },
          { "started_utc", FormatUtcTimestamp(session_started) },
          { "ended_utc", FormatUtcTimestamp(session_ended) },
          { "tool_version", std::string(OXYGEN_IMPORT_TOOL_VERSION) },
          { "command_line", std::string(command_line) },
          { "cooked_root", cooked_root.string() },
        } } });

    const auto stats_json = BuildStatsJson(report_copy.telemetry);
    const auto time_ms_io = ComputeIoMillis(report_copy.telemetry);
    const auto time_ms_cpu = ComputeCpuMillis(report_copy.telemetry);

    payload.update({ { "summary",
      {
        { "jobs_total", 1 },
        { "jobs_succeeded", report_copy.success ? 1 : 0 },
        { "jobs_failed", report_copy.success ? 0 : 1 },
        { "jobs_skipped", 0 },
        { "time_ms_total", elapsed_ms },
        { "time_ms_io", time_ms_io },
        { "time_ms_cpu", time_ms_cpu },
      } } });

    const auto job_type = ResolveReportJobType(request);
    ordered_json job = ordered_json::object();
    job.update({ { "index", 1 } });
    job.update({ { "type", job_type } });
    job.update({ { "work_items",
      BuildWorkItemsJson(
        progress_trace, job_type, request.source_path.string()) } });
    job.update({ { "status", std::string(JobStatusFromReport(report_copy)) } });
    job.update({ { "outputs", BuildOutputsJson(report_copy.outputs) } });
    job.update({ { "cooked_root", report_copy.cooked_root.generic_string() } });
    if (report_copy.retained_record_path) {
      job.update({ { "retained_record_path",
        report_copy.retained_record_path->generic_string() } });
    }
    if (report_copy.previous_cooked_root) {
      job.update({ { "previous_cooked_root",
        report_copy.previous_cooked_root->generic_string() } });
    }
    if (report_copy.success
      && !report_copy.material_slot_provenance_json.empty()) {
      job.update({ { "material_slot_provenance",
        ordered_json::parse(report_copy.material_slot_provenance_json) } });
    }
    job.update({ { "stats", stats_json } });
    job.update(
      { { "diagnostics", BuildDiagnosticsJson(report_copy.diagnostics) } });
    payload.update({ { "jobs", ordered_json::array({ std::move(job) }) } });

    if (!WriteJsonReport(payload, *resolved_path, writer)) {
      return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    written_report = *resolved_path;
  }

  if (!report_copy.success) {
    writer->Error("ERROR: import failed");
    for (const auto& diag : report_copy.diagnostics) {
      const auto message = fmt::format("- {}: {}", diag.code, diag.message);
      switch (diag.severity) {
      case ImportSeverity::kInfo:
        writer->Info(message);
        break;
      case ImportSeverity::kWarning:
        writer->Warning(message);
        break;
      case ImportSeverity::kError:
        writer->Error(message);
        break;
      }
    }
    if (written_report.has_value()) {
      writer->Info(fmt::format("Report written: {}", written_report->string()));
    }
    return std::unexpected(
      std::make_error_code(std::errc::state_not_recoverable));
  }

  writer->Report("OK: import complete");
  if (written_report.has_value()) {
    writer->Info(fmt::format("Report written: {}", written_report->string()));
  }
  return {};
}

auto RunSceneImportJob(const SceneImportSettings& settings,
  const ImportFormat format, const GlobalOptions& globals)
  -> std::expected<void, std::error_code>
{
  try {
    const auto apply_recipe = !settings.recipe_path.empty();
    const auto replay = settings.source_path.empty()
      && !settings.retained_record_path.empty() && !apply_recipe;
    if (apply_recipe
      && (!settings.source_path.empty() || settings.retained_record_path.empty()
        || settings.content_root.empty())) {
      throw std::invalid_argument(
        "--recipe requires --record and --content-root, without a positional "
        "source");
    }
    if (replay && !settings.content_root.empty()) {
      throw std::invalid_argument("Record replay uses its saved Content root");
    }
    if (apply_recipe || replay) {
      if (!globals.parsed_options) {
        throw std::logic_error(
          "Retained CLI validation requires parsed option presence");
      }
      constexpr auto kContentOverrides = std::array {
        "source",
        "output",
        "name",
        "material-slot-source-identity",
        "material-slot-provenance",
        "no-import-textures",
        "no-import-materials",
        "no-import-geometry",
        "no-import-scene",
        "unit-policy",
        "unit-scale",
        "no-bake-transforms",
        "normals",
        "tangents",
        "prune-nodes",
        "content-hashing",
        "omitted-light-range",
      };
      for (const auto* option : kContentOverrides) {
        if (globals.parsed_options->HasOption(option)
          && std::ranges::any_of(globals.parsed_options->ValuesOf(option),
            [](const auto& value) -> bool { return !value.IsDefaulted(); })) {
          throw std::invalid_argument(
            std::string("Recipe application and replay do not accept --")
            + option);
        }
      }
      if (replay && globals.parsed_options->HasOption("content-root")) {
        throw std::invalid_argument(
          "Record replay uses its saved Content root");
      }
    }
    auto record_path = std::filesystem::path(settings.retained_record_path);
    if (record_path.empty() && !settings.content_root.empty()
      && !settings.source_path.empty()) {
      record_path = RetainedModelImport::RecordPath(
        settings.content_root, settings.source_path);
    }
    if (!record_path.empty()) {
      if (!settings.material_slot_source_identity.empty()
        || !settings.material_slot_provenance_json.empty()
        || !settings.material_slot_provenance_path.empty()) {
        throw std::invalid_argument(
          "Retained identity belongs to the record; omit source identity and "
          "provenance options");
      }
      if (!settings.cooked_root.empty()) {
        throw std::invalid_argument(
          "Retained output is derived from Content; do not specify --output");
      }
      if (apply_recipe) {
        RetainedModelImport::SaveRecipeFile(
          record_path, settings.content_root, settings.recipe_path, format);
      } else if (!settings.source_path.empty()) {
        ImportRequest source;
        source.source_path = settings.source_path;
        if (source.GetFormat() != format) {
          throw std::invalid_argument(
            "Source format does not match the import command");
        }
        if (settings.content_root.empty()) {
          throw std::invalid_argument(
            "Creating or updating a retained recipe requires --content-root");
        }
        const auto recipe
          = RetainedModelImport::MakeRecipe(settings, LooseCookedLayout {});
        RetainedModelImport::SaveRecipe(
          record_path, settings.content_root, recipe);
      }
      const auto publication = RetainedModelImport::Prepare(record_path);
      if (publication->Request().GetFormat() != format) {
        throw std::invalid_argument(
          "Retained recipe format does not match this command");
      }
      return RunImportJob(publication->Request(), globals.writer,
        settings.report_path, globals.command_line, !globals.no_tui,
        globals.import_service, publication);
    }
    std::ostringstream errors;
    const auto request = internal::BuildSceneRequest(settings, format, errors);
    if (!request) {
      globals.writer->Error(errors.str());
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return RunImportJob(*request, globals.writer, settings.report_path,
      globals.command_line, !globals.no_tui, globals.import_service);
  } catch (const std::system_error& error) {
    globals.writer->Error(error.what());
    return std::unexpected(error.code());
  } catch (const std::exception& error) {
    globals.writer->Error(error.what());
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
}

} // namespace oxygen::content::import::tool
