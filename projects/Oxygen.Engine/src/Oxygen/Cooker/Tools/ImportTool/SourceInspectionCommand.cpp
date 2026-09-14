//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>

#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/SceneSourceInspection.h>
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
    .About("Inspect static/scalar source metadata and external dependencies")
    .WithPositionalArguments(source)
    .WithOption(std::move(output));
}

auto SourceInspectionCommand::Run() -> std::expected<void, std::error_code>
{
  const auto source = std::filesystem::path(source_path_);
  const auto report = InspectSceneSource(source);
  const auto output = std::filesystem::path(output_path_);
  const auto aliases_output = [&](const std::filesystem::path& input) -> bool {
    return std::filesystem::weakly_canonical(input)
      == std::filesystem::weakly_canonical(output)
      || (std::filesystem::exists(input) && std::filesystem::exists(output)
        && std::filesystem::equivalent(input, output));
  };
  if (aliases_output(source)) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  for (const auto& dependency : report.external_files) {
    if (aliases_output(source.parent_path() / dependency)) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }
  if (const auto parent = output.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  auto stream = std::ofstream(output, std::ios::binary | std::ios::trunc);
  stream << ExportSceneSourceInspection(report) << '\n';
  stream.close();
  if (!stream) {
    return std::unexpected(std::make_error_code(std::errc::io_error));
  }
  return {};
}

} // namespace oxygen::content::import::tool
