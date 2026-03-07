//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>

#include <Oxygen/Cooker/Tools/PakTool/CliBuilder.h>

namespace oxygen::content::pak::tool {

namespace {

  using oxygen::clap::CliBuilder;
  using oxygen::clap::Command;
  using oxygen::clap::CommandBuilder;

  constexpr auto kProgramName = std::string_view { "Oxygen.Cooker.PakTool" };

} // namespace

auto BuildCli() -> std::unique_ptr<oxygen::clap::Cli>
{
  const auto build_command = std::shared_ptr<Command>(CommandBuilder("build")
      .About("Build a full pak archive from cooked sources.")
      .Build()
      .release());
  const auto patch_command = std::shared_ptr<Command>(CommandBuilder("patch")
      .About("Build a patch pak archive against base catalogs.")
      .Build()
      .release());

  return CliBuilder()
    .ProgramName(std::string(kProgramName))
    .Version(std::string(OXYGEN_PAKTOOL_VERSION))
    .About("Build and publish Oxygen pak archives and their sidecar artifacts.")
    .WithHelpCommand()
    .WithVersionCommand()
    .WithCommand(build_command)
    .WithCommand(patch_command)
    .Build();
}

} // namespace oxygen::content::pak::tool
