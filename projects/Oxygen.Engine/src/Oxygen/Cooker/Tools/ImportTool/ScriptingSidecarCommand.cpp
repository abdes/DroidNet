//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ScriptingSidecarCommand.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;

} // namespace

auto ScriptingSidecarCommand::Name() const -> std::string_view
{
  return "script-sidecar";
}

auto ScriptingSidecarCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto source_path = Option::Positional("source")
                       .About("Path to the scripting sidecar source file "
                              "(optional when --bindings-inline is provided)")
                       .WithValue<std::string>()
                       .DefaultValue(std::string {})
                       .StoreTo(&options_.source_path)
                       .Build();

  auto bindings_inline
    = Option::WithKey("bindings-inline")
        .About("Inline sidecar bindings JSON payload (array of bindings or "
               "{ \"bindings\": [...] } object)")
        .Long("bindings-inline")
        .WithValue<std::string>()
        .StoreTo(&options_.inline_bindings_json)
        .Build();

  auto cooked_root = Option::WithKey("output")
                       .About("Destination cooked root directory")
                       .Short("i")
                       .Long("output")
                       .WithValue<std::string>()
                       .StoreTo(&options_.cooked_root)
                       .Build();

  auto job_name = Option::WithKey("name")
                    .About("Optional job name")
                    .Long("name")
                    .WithValue<std::string>()
                    .StoreTo(&options_.job_name)
                    .Build();

  auto report
    = Option::WithKey("report")
        .About("Write a JSON report (absolute or relative to working dir)")
        .Long("report")
        .WithValue<std::string>()
        .StoreTo(&options_.report_path)
        .Build();

  auto target_scene_virtual_path
    = Option::WithKey("target-scene-virtual-path")
        .About("Canonical target scene virtual path")
        .Long("target-scene-virtual-path")
        .WithValue<std::string>()
        .StoreTo(&options_.target_scene_virtual_path)
        .Build();

  return CommandBuilder("script-sidecar")
    .About("Import a scene scripting sidecar")
    .WithPositionalArguments(source_path)
    .WithOption(std::move(cooked_root))
    .WithOption(std::move(job_name))
    .WithOption(std::move(report))
    .WithOption(std::move(target_scene_virtual_path))
    .WithOption(std::move(bindings_inline));
}

auto ScriptingSidecarCommand::Execute() -> std::expected<void, std::error_code>
{
  auto settings = options_;

  if (global_options_ != nullptr && settings.cooked_root.empty()) {
    settings.cooked_root = global_options_->cooked_root;
  }

  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto writer = global_options_->writer;

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto request
    = internal::BuildScriptingSidecarRequest(settings, diagnostics);
  if (!request.has_value()) {
    writer->Error(internal::FormatDiagnostics(diagnostics));
    return RefuseImportJob(
      {
        .report_path = settings.report_path,
        .command_line = global_options_->command_line,
        .job_type = "script-sidecar",
        .source_path = settings.source_path,
        .cooked_root = settings.cooked_root,
        .diagnostics = diagnostics,
      },
      writer);
  }

  return RunImportJob(*request, writer, settings.report_path,
    global_options_->command_line, !global_options_->no_tui,
    global_options_->import_service);
}

} // namespace oxygen::content::import::tool
