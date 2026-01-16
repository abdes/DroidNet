//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <iostream>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>

#include <Oxygen/Content/Tools/ImportTool/CliBuilder.h>
#include <Oxygen/Content/Tools/ImportTool/ImportCommand.h>
#include <Oxygen/Content/Tools/ImportTool/TextureCommand.h>

auto main(int argc, char** argv) -> int
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

  loguru::init(argc, const_cast<const char**>(argv));
  loguru::set_thread_name("main");

  int exit_code = 0;
  try {
    using oxygen::clap::Command;
    using oxygen::content::import::tool::BuildCli;
    using oxygen::content::import::tool::ImportCommand;
    using oxygen::content::import::tool::TextureCommand;

    TextureCommand texture_command;
    std::vector<ImportCommand*> commands { &texture_command };

    const auto cli = BuildCli(commands);
    const auto context = cli->Parse(argc, const_cast<const char**>(argv));

    const auto command_path = context.active_command->PathAsString();
    const auto& ovm = context.ovm;

    if (command_path == Command::VERSION || command_path == Command::HELP
      || ovm.HasOption(Command::HELP)) {
      exit_code = 0;
    } else {
      bool handled = false;
      for (auto* command : commands) {
        if (command_path == command->Name()) {
          exit_code = command->Run();
          handled = true;
          break;
        }
      }

      if (!handled) {
        std::cerr << "ERROR: Unknown command\n";
        exit_code = 1;
      }
    }
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    exit_code = 2;
  }

  loguru::flush();
  loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
  loguru::shutdown();

  return exit_code;
}
