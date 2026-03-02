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
#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Cooker/Tools/ImportTool/InputCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;

} // namespace

auto InputCommand::Name() const -> std::string_view { return "input"; }

auto InputCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto source_path = Option::Positional("source")
                       .About("Path to the input source JSON file")
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

  return CommandBuilder("input")
    .About("Import input authoring JSON")
    .WithPositionalArguments(source_path)
    .WithOption(std::move(cooked_root))
    .WithOption(std::move(job_name))
    .WithOption(std::move(report))
    .WithOption(std::move(with_content_hashing));
}

auto InputCommand::Run() -> std::expected<void, std::error_code>
{
  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto writer = global_options_->writer;

  auto effective_cooked_root = options_.cooked_root;
  if (effective_cooked_root.empty() && global_options_ != nullptr) {
    effective_cooked_root = global_options_->cooked_root;
  }

  auto settings = InputImportSettings {
    .source_path = options_.source_path,
    .cooked_root = effective_cooked_root,
  };

  std::ostringstream err;
  auto request = internal::BuildInputImportRequest(settings, err);
  if (!request.has_value()) {
    const auto msg = err.str();
    if (!msg.empty()) {
      writer->Error(msg);
    }
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  if (!options_.job_name.empty()) {
    request->job_name = options_.job_name;
  }
  request->options.with_content_hashing = options_.with_content_hashing;

  return RunImportJob(*request, writer, options_.report_path,
    global_options_->command_line, !global_options_->no_tui,
    global_options_->import_service);
}

} // namespace oxygen::content::import::tool
