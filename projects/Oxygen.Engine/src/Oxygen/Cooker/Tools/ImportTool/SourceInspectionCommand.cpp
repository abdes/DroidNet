//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/SceneSourceInspection.h>
#include <Oxygen/Cooker/Tools/Common/ReportFile.h>
#include <Oxygen/Cooker/Tools/ImportTool/ReportJson.h>
#include <Oxygen/Cooker/Tools/ImportTool/SourceInspectionCommand.h>

namespace oxygen::content::import::tool {

auto SourceInspectionCommand::Name() const -> std::string_view
{
  return "inspect-source";
}

auto SourceInspectionCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto source = clap::Option::Positional("source")
                  .About("glTF, GLB or FBX source to inspect")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&source_path_)
                  .Build();
  auto output = clap::Option::WithKey("output")
                  .About("Destination JSON report path")
                  .Long("output")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&output_path_)
                  .Build();
  return clap::CommandBuilder("inspect-source")
    .About("Inspect static model metadata and external dependencies")
    .WithPositionalArguments(source)
    .WithOption(std::move(output));
}

auto SourceInspectionCommand::Execute() -> std::expected<void, std::error_code>
{
  const auto source = std::filesystem::path(source_path_);
  const auto report = InspectSceneSource(source);
  const auto writer = global_options_->writer;
  auto guard = toolkit::ReportGuard {
    .inputs = { source },
    .cooked_roots = {},
  };
  for (const auto& dependency : report.external_files) {
    guard.inputs.push_back(source.parent_path() / dependency);
  }
  if (!IsReportDestinationSafe(output_path_, guard, writer)) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (!PublishImportReport(
        output_path_, ExportSceneSourceInspection(report) + '\n', writer)) {
    return std::unexpected(std::make_error_code(std::errc::io_error));
  }
  return {};
}

} // namespace oxygen::content::import::tool
