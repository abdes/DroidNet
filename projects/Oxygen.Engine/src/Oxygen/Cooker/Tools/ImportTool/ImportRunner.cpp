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
#include <memory>
#include <mutex>
#include <optional>
#include <ratio>
#include <span>
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
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/ModelImportRecordError.h>
#include <Oxygen/Cooker/Tools/Common/ReportFile.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ReportJson.h>
#include <Oxygen/Cooker/Tools/ImportTool/SourceMountOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/UI/JobViewModel.h>
#include <Oxygen/Cooker/Tools/ImportTool/UI/Screens/ImportScreen.h>

#ifndef OXYGEN_IMPORT_TOOL_VERSION
#  error OXYGEN_IMPORT_TOOL_VERSION must be defined for ImportTool reports.
#endif

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::ordered_json;

  //! A file path as reports spell it: absolute and lexically normal.
  auto AbsolutePath(const std::filesystem::path& path) -> std::filesystem::path
  {
    return path.empty() ? path : toolkit::ResolveReportPath(path);
  }

  //! The cooked sources a request reads and writes.
  auto CookedRootsOf(const ImportRequest& request)
    -> std::vector<std::filesystem::path>
  {
    auto roots = request.cooked_context_roots;
    roots.push_back(request.ResolveCookedRoot());
    return roots;
  }

  auto MakeError(const std::string_view code, std::string message,
    const std::filesystem::path& source_path) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::string { code },
      .message = std::move(message),
      .source_path = source_path.string(),
    };
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

auto RefuseImportJob(const ImportRefusal& refusal,
  const oxygen::observer_ptr<IMessageWriter> writer)
  -> std::unexpected<std::error_code>
{
  const auto refused
    = std::unexpected(std::make_error_code(std::errc::invalid_argument));
  if (refusal.report_path.empty()
    || !IsReportDestinationSafe(refusal.report_path,
      {
        .inputs = { refusal.source_path },
        .cooked_roots = { refusal.cooked_root },
      },
      writer)) {
    return refused;
  }

  const auto now = std::chrono::system_clock::now();
  ordered_json session = ordered_json::object();
  session.update({
    { "id", MakeSessionId(now) },
    { "started_utc", FormatUtcTimestamp(now) },
    { "ended_utc", FormatUtcTimestamp(now) },
    { "tool_version", std::string(OXYGEN_IMPORT_TOOL_VERSION) },
    { "command_line", std::string(refusal.command_line) },
  });
  if (!refusal.cooked_root.empty()) {
    session.update({
      {
        "cooked_root",
        std::filesystem::absolute(refusal.cooked_root)
          .lexically_normal()
          .string(),
      },
    });
  }

  // A refusal names its source as the command line spelled it.
  auto diagnostics = std::vector<ImportDiagnostic>(
    refusal.diagnostics.begin(), refusal.diagnostics.end());
  for (auto& diagnostic : diagnostics) {
    if (diagnostic.source_path == refusal.source_path.string()) {
      diagnostic.source_path = AbsolutePath(refusal.source_path).string();
    }
  }

  ordered_json job = ordered_json::object();
  job.update({
    { "index", 1 },
    { "type", std::string(refusal.job_type) },
    { "work_items", ordered_json::array() },
    { "status", "failed" },
    { "outputs", ordered_json::array() },
    { "products", ordered_json::array() },
    { "stats", BuildEmptyStatsJson() },
    { "diagnostics", BuildDiagnosticsJson(diagnostics) },
  });

  ordered_json payload = ordered_json::object();
  payload.update({
    { "report_version", std::string(kReportVersion) },
    { "session", std::move(session) },
    {
      "summary",
      {
        { "jobs_total", 1 },
        { "jobs_succeeded", 0 },
        { "jobs_failed", 1 },
        { "jobs_skipped", 0 },
        { "time_ms_total", 0.0 },
        { "time_ms_io", 0.0 },
        { "time_ms_cpu", 0.0 },
      },
    },
    { "jobs", ordered_json::array({ std::move(job) }) },
  });
  static_cast<void>(
    PublishImportReport(refusal.report_path, payload.dump(2) + "\n", writer));
  return refused;
}

auto RunImportJob(const ImportRequest& command_request,
  oxygen::observer_ptr<IMessageWriter> writer,
  const std::string_view report_path, const std::string_view command_line,
  const bool enable_tui, oxygen::observer_ptr<AsyncImportService> service,
  std::shared_ptr<ModelImportRecord> publication)
  -> std::expected<void, std::error_code>
{
  const auto start_time = std::chrono::steady_clock::now();
  const auto session_started = std::chrono::system_clock::now();
  DCHECK_NOTNULL_F(writer, "Message writer must be provided by main");
  DCHECK_NOTNULL_F(service, "Import service must be provided by main");
  CHECK_F(!command_line.empty(), "Command line is required for report output");

  // The job's paths reach its report, whatever the command line's spelling.
  auto request = command_request;
  request.source_path = AbsolutePath(request.source_path);
  if (request.cooked_root.has_value()) {
    request.cooked_root = AbsolutePath(*request.cooked_root);
  }

  if (!IsReportDestinationSafe(report_path,
        {
          .inputs = { request.source_path },
          .cooked_roots = CookedRootsOf(request),
        },
        writer)) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

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
          recent_logs.emplace_back("Job Started");
        } else if (progress.header.kind == ProgressEventKind::kJobFinished) {
          recent_logs.emplace_back("Job Finished");
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

    LOG_F(
      INFO, "ImportTool submit job: source='{}'", request.source_path.string());
    const auto job_id = publication
      ? service->SubmitModelImport(
          std::move(publication), on_complete, on_progress)
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
          vm.status = "Running";
          if (report.has_value()) {
            vm.status = report->success ? "Completed" : "Failed";
          }
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
    const auto failure = MakeError(
      import_tool_codes::kSubmitFailed, *submit_error, request.source_path);
    static_cast<void>(RefuseImportJob(
      {
        .report_path = report_path,
        .command_line = command_line,
        .job_type = ResolveReportJobType(request),
        .source_path = request.source_path,
        .cooked_root = request.ResolveCookedRoot(),
        .diagnostics = std::span { &failure, 1U },
      },
      writer));
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
    const auto session_ended = std::chrono::system_clock::now();
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start_time)
                              .count();
    CHECK_F(!cooked_root.empty(), "Cooked root is required in report output");

    ordered_json payload = ordered_json::object();
    payload.update({
      { "report_version", std::string(kReportVersion) },
      {
        "session",
        {
          { "id", MakeSessionId(session_started) },
          { "started_utc", FormatUtcTimestamp(session_started) },
          { "ended_utc", FormatUtcTimestamp(session_ended) },
          { "tool_version", std::string(OXYGEN_IMPORT_TOOL_VERSION) },
          { "command_line", std::string(command_line) },
          { "cooked_root", cooked_root.string() },
        },
      },
    });

    const auto stats_json = BuildStatsJson(report_copy.telemetry);
    const auto time_ms_io = ComputeIoMillis(report_copy.telemetry);
    const auto time_ms_cpu = ComputeCpuMillis(report_copy.telemetry);

    payload.update({
      {
        "summary",
        {
          { "jobs_total", 1 },
          { "jobs_succeeded", report_copy.success ? 1 : 0 },
          { "jobs_failed", report_copy.success ? 0 : 1 },
          { "jobs_skipped", 0 },
          { "time_ms_total", elapsed_ms },
          { "time_ms_io", time_ms_io },
          { "time_ms_cpu", time_ms_cpu },
        },
      },
    });

    const auto job_type = ResolveReportJobType(request);
    ordered_json job = ordered_json::object();
    job.update({ { "index", 1 } });
    job.update({ { "type", job_type } });
    if (auto importer = BuildImporterJson(request); importer.has_value()) {
      job.update({ { "importer", std::move(*importer) } });
    }
    job.update({
      {
        "work_items",
        BuildWorkItemsJson(
          progress_trace, job_type, request.source_path.string()),
      },
    });
    job.update({ { "status", std::string(JobStatusFromReport(report_copy)) } });
    job.update({ { "outputs", BuildOutputsJson(report_copy.outputs) } });
    job.update({ { "products", BuildProductsJson(report_copy.products) } });
    job.update({ { "cooked_root", report_copy.cooked_root.string() } });
    if (report_copy.model_record_path) {
      job.update({
        {
          "model_record_path",
          report_copy.model_record_path->string(),
        },
      });
    }
    if (report_copy.previous_cooked_root) {
      job.update({
        {
          "previous_cooked_root",
          report_copy.previous_cooked_root->string(),
        },
      });
    }
    if (const auto update
      = ModelImportRecord::SerializeImportUpdate(request, report_copy);
      !update.empty()) {
      job.update({ { "model_import", ordered_json::parse(update) } });
    }
    job.update({ { "stats", stats_json } });
    job.update(
      { { "diagnostics", BuildDiagnosticsJson(report_copy.diagnostics) } });
    payload.update({ { "jobs", ordered_json::array({ std::move(job) }) } });

    if (!PublishImportReport(report_path, payload.dump(2) + "\n", writer)) {
      return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    written_report = toolkit::ResolveReportPath(report_path);
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
  const ImportFormat format, const SourceMountOptions& mount_options,
  const GlobalOptions& globals) -> std::expected<void, std::error_code>
{
  const auto refuse = [&](const std::span<const ImportDiagnostic> diagnostics)
    -> std::unexpected<std::error_code> {
    return RefuseImportJob(
      {
        .report_path = settings.report_path,
        .command_line = globals.command_line,
        .job_type = to_string(format),
        .source_path = settings.source_path,
        .cooked_root = settings.cooked_root,
        .diagnostics = diagnostics,
      },
      globals.writer);
  };
  auto diagnostics = std::vector<ImportDiagnostic> {};
  try {
    // Records are found, and their sources located, through the mounts of
    // the project at `--root`, the working directory by default.
    const auto mounts
      = ResolveSourceMounts(mount_options, globals.writer, diagnostics);
    if (!mounts.has_value()) {
      return refuse(diagnostics);
    }
    // `--record` names a record; `--root` makes the import a record import,
    // its record at the `--virtual-path` identity.
    const auto apply_recipe = !settings.recipe_path.empty();
    const auto by_identity
      = settings.model_record_path.empty() && !mount_options.root.empty();
    const auto publishes_record
      = apply_recipe || !settings.model_record_path.empty() || by_identity;
    const auto replay
      = publishes_record && settings.source_path.empty() && !apply_recipe;
    if (apply_recipe && !settings.source_path.empty()) {
      throw std::invalid_argument("--recipe takes no positional source");
    }
    // A record import publishes into a generations folder this tool owns,
    // checked before anything writes the record.
    if (publishes_record && settings.cooked_root.empty()) {
      throw std::invalid_argument(
        "Record publication needs --output, a generations folder this tool "
        "owns; the project's own output folders belong to its publisher");
    }
    if (apply_recipe || replay) {
      if (!globals.parsed_options) {
        throw std::logic_error(
          "Model import CLI validation requires parsed option presence");
      }
      constexpr auto kContentOverrides = std::array {
        "source",
        "name",
        "no-import-textures",
        "no-import-materials",
        "no-import-geometry",
        "no-import-scene",
        "unit-policy",
        "unit-scale",
        "bake-transforms",
        "normals",
        "tangents",
        "prune-nodes",
        "omitted-light-range",
        "gltf-scene",
        "up-axis",
        "smoothing-angle",
        "no-import-lights",
        "no-import-cameras",
        "light-intensity-scale",
        "max-texture-size",
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
    }
    auto record_path = std::filesystem::path(settings.model_record_path);
    auto notes = std::vector<ImportDiagnostic> {};
    if (by_identity && !apply_recipe) {
      if (settings.virtual_path.empty()) {
        throw std::invalid_argument(
          "--root imports need --virtual-path, the model's identity");
      }
      if (settings.source_path.empty()) {
        auto findings = std::vector<ImportDiagnostic> {};
        const auto path = ModelImportRecord::RecordPathOf(
          *mounts, settings.virtual_path, findings);
        if (!path.has_value()) {
          throw ModelImportRecordError(findings.front());
        }
        record_path = *path;
      } else {
        auto location = ModelImportRecord::LocateRecord({
          .source = settings.source_path,
          .identity = settings.virtual_path,
          .mounts = make_observer(&*mounts),
        });
        record_path = std::move(location.record_path);
        notes = std::move(location.diagnostics);
      }
    } else if (!record_path.empty() && !settings.virtual_path.empty()) {
      auto findings = std::vector<ImportDiagnostic> {};
      const auto identity
        = ModelImportRecord::IdentityOf(*mounts, record_path, findings);
      if (!identity.has_value()) {
        throw ModelImportRecordError(findings.front());
      }
      if (*identity != settings.virtual_path) {
        throw std::invalid_argument("--record " + record_path.generic_string()
          + " is the record of " + *identity + ", not " + settings.virtual_path
          + ": a record's identity is where it lives");
      }
    }
    if (apply_recipe) {
      auto location = ModelImportRecord::SaveRecipeFile(
        {
          .record_path = record_path,
          .source = {},
          .settings_json = std::nullopt,
          .mounts = make_observer(&*mounts),
        },
        settings.recipe_path, format);
      record_path = std::move(location.record_path);
      notes = std::move(location.diagnostics);
    }
    if (!record_path.empty()) {
      if (!IsReportDestinationSafe(settings.report_path,
            { .inputs = { record_path }, .cooked_roots = {} },
            globals.writer)) {
        return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
      }
      if (!settings.source_path.empty()) {
        ImportRequest source;
        source.source_path = settings.source_path;
        if (source.GetFormat() != format) {
          throw std::invalid_argument(
            "Source format does not match the import command");
        }
        ModelImportRecord::Save({
          .record_path = record_path,
          .source = settings.source_path,
          .settings_json = ModelImportRecord::MakeSettings(settings),
          .mounts = make_observer(&*mounts),
        });
      }
      const auto publication = ModelImportRecord::Prepare(record_path,
        ModelImportGenerations { .root = settings.cooked_root }, *mounts,
        std::move(notes));
      if (publication->Request().GetFormat() != format) {
        throw std::invalid_argument(
          "The record's source format does not match this command");
      }
      return RunImportJob(publication->Request(), globals.writer,
        settings.report_path, globals.command_line, !globals.no_tui,
        globals.import_service, publication);
    }
    auto request = internal::BuildSceneRequest(settings, format, diagnostics);
    if (!request) {
      globals.writer->Error(internal::FormatDiagnostics(diagnostics));
      return refuse(diagnostics);
    }
    if (!LocateRequestSource(*request, *mounts, globals.writer, diagnostics)) {
      return refuse(diagnostics);
    }
    return RunImportJob(*request, globals.writer, settings.report_path,
      globals.command_line, !globals.no_tui, globals.import_service);
  } catch (const ModelImportRecordError& error) {
    globals.writer->Error(error.what());
    return refuse(std::span { &error.Diagnostic(), 1U });
  } catch (const std::system_error& error) {
    globals.writer->Error(error.what());
    const auto failure = MakeError(
      import_tool_codes::kCommandFailed, error.what(), settings.source_path);
    static_cast<void>(refuse(std::span { &failure, 1U }));
    return std::unexpected(error.code());
  } catch (const std::exception& error) {
    globals.writer->Error(error.what());
    const auto failure = MakeError(
      import_tool_codes::kOptionInvalid, error.what(), settings.source_path);
    return refuse(std::span { &failure, 1U });
  }
}

} // namespace oxygen::content::import::tool
