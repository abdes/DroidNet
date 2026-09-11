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
#include <Oxygen/Cooker/Import/BuiltinGeometryCatalog.h>
#include <Oxygen/Cooker/Tools/ImportTool/BuiltinCatalogCommand.h>

namespace oxygen::content::import::tool {

auto BuiltinCatalogCommand::Name() const -> std::string_view
{
  return "builtin-catalog";
}

auto BuiltinCatalogCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto output = clap::Option::Positional("output")
                  .About("Destination JSON catalog path")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&output_path_)
                  .Build();
  auto mount
    = clap::Option::WithKey("mount")
        .About("Virtual mount for generated geometry and default material")
        .Long("mount")
        .WithValue<std::string>()
        .DefaultValue("Content")
        .StoreTo(&mount_name_)
        .Build();
  return clap::CommandBuilder("builtin-catalog")
    .About("Export engine-owned built-in geometry definitions")
    .WithPositionalArguments(output)
    .WithOption(std::move(mount));
}

auto BuiltinCatalogCommand::Run() -> std::expected<void, std::error_code>
{
  const auto catalog = ExportBuiltinGeometryCatalog(mount_name_);
  const auto output = std::filesystem::path(output_path_);
  if (const auto parent = output.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  auto stream = std::ofstream(output, std::ios::binary | std::ios::trunc);
  stream << catalog << '\n';
  stream.close();
  if (!stream) {
    return std::unexpected(std::make_error_code(std::errc::io_error));
  }
  return {};
}

} // namespace oxygen::content::import::tool
