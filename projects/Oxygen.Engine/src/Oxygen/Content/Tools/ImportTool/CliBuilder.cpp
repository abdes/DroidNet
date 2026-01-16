//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Tools/ImportTool/CliBuilder.h>

#include <string>

#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Content/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

namespace {

  constexpr std::string_view kProgramName = "Oxygen.Content.ImportTool";
  constexpr std::string_view kVersion = "0.1";

} // namespace

auto BuildCli(std::span<ImportCommand* const> commands)
  -> std::unique_ptr<clap::Cli>
{
  clap::CliBuilder builder;
  builder.ProgramName(std::string(kProgramName))
    .Version(std::string(kVersion))
    .About("Invoke async import jobs for standalone assets")
    .WithHelpCommand()
    .WithVersionCommand();

  for (auto* command : commands) {
    builder.WithCommand(command->BuildCommand());
  }

  return builder.Build();
}

} // namespace oxygen::content::import::tool
