//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ScriptCommand.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;

} // namespace

auto ScriptCommand::Name() const -> std::string_view { return "script"; }

auto ScriptCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto source_path = Option::Positional("source")
                       .About("Path to the source script file")
                       .Required()
                       .WithValue<std::string>()
                       .StoreTo(&options_.source_path)
                       .Build();

  auto cooked_root = Option::WithKey("output")
                       .About("Destination cooked root directory")
                       .Short("i")
                       .Long("output")
                       .WithValue<std::string>()
                       .StoreTo(&options_.cooked_root)
                       .Build();

  auto with_content_hashing
    = Option::WithKey("content-hashing")
        .About("Enable or disable content hashing for outputs")
        .Long("content-hashing")
        .WithValue<bool>()
        .StoreTo(&options_.with_content_hashing)
        .Build();

  auto job_name = Option::WithKey("name")
                    .About("Optional job name")
                    .Long("name")
                    .WithValue<std::string>()
                    .StoreTo(&options_.job_name)
                    .Build();

  auto report
    = Option::WithKey("report")
        .About("Write a JSON report (absolute or relative to cooked root)")
        .Long("report")
        .WithValue<std::string>()
        .StoreTo(&options_.report_path)
        .Build();

  auto compile_scripts = Option::WithKey("compile")
                           .About("Compile script source during import")
                           .Long("compile")
                           .WithValue<bool>()
                           .StoreTo(&options_.compile_scripts)
                           .Build();

  auto compile_mode = Option::WithKey("compile-mode")
                        .About("Script compile mode (debug or optimized)")
                        .Long("compile-mode")
                        .WithValue<std::string>()
                        .StoreTo(&options_.compile_mode)
                        .Build();

  auto script_storage = Option::WithKey("script-storage")
                          .About("Script storage mode (embedded or external)")
                          .Long("script-storage")
                          .WithValue<std::string>()
                          .StoreTo(&options_.script_storage)
                          .Build();

  return CommandBuilder("script")
    .About("Import a standalone script asset")
    .WithPositionalArguments(source_path)
    .WithOption(std::move(cooked_root))
    .WithOption(std::move(job_name))
    .WithOption(std::move(report))
    .WithOption(std::move(compile_scripts))
    .WithOption(std::move(compile_mode))
    .WithOption(std::move(script_storage))
    .WithOption(std::move(with_content_hashing));
}

auto ScriptCommand::Run() -> std::expected<void, std::error_code>
{
  auto settings = options_;

  if (global_options_ != nullptr && settings.cooked_root.empty()) {
    settings.cooked_root = global_options_->cooked_root;
  }

  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto writer = global_options_->writer;

  std::ostringstream err;
  const auto request = internal::BuildScriptAssetRequest(settings, err);
  if (!request.has_value()) {
    const auto msg = err.str();
    if (!msg.empty()) {
      writer->Error(msg);
    }
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  return RunImportJob(*request, writer, settings.report_path,
    global_options_->command_line, !global_options_->no_tui,
    global_options_->import_service);
}

} // namespace oxygen::content::import::tool
