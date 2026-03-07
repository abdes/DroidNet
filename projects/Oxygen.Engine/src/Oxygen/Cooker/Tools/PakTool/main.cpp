//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <iostream>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>

#include <Oxygen/Cooker/Tools/PakTool/CliBuilder.h>

auto main(int argc, char** argv) -> int
{
  try {
    const auto cli = oxygen::content::pak::tool::BuildCli();
    const auto context = cli->Parse(argc, const_cast<const char**>(argv));

    const auto command_path = context.active_command->PathAsString();
    if (command_path == oxygen::clap::Command::VERSION
      || command_path == oxygen::clap::Command::HELP
      || context.ovm.HasOption(oxygen::clap::Command::HELP)) {
      return 0;
    }

    std::cerr << "ERROR: command implementation pending: " << command_path
              << "\n";
    return 1;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "ERROR: unknown exception\n";
    return 1;
  }
}
