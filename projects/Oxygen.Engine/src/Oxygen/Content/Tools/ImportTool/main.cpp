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
#include <Oxygen/Content/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Content/Tools/ImportTool/CliBuilder.h>
#include <Oxygen/Content/Tools/ImportTool/FbxCommand.h>
#include <Oxygen/Content/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Content/Tools/ImportTool/GltfCommand.h>
#include <Oxygen/Content/Tools/ImportTool/ImportCommand.h>
#include <Oxygen/Content/Tools/ImportTool/TextureCommand.h>

auto main(int argc, char** argv) -> int
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = true;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

  loguru::init(argc, const_cast<const char**>(argv));
  loguru::set_thread_name("main");

  int exit_code = 0;
  try {
    using oxygen::clap::Command;
    using oxygen::content::import::tool::BatchCommand;
    using oxygen::content::import::tool::BuildCli;
    using oxygen::content::import::tool::FbxCommand;
    using oxygen::content::import::tool::GltfCommand;
    using oxygen::content::import::tool::ImportCommand;
    using oxygen::content::import::tool::TextureCommand;

    using oxygen::content::import::tool::GlobalOptions;

    GlobalOptions global_options;
    BatchCommand batch_command(&global_options);
    FbxCommand fbx_command(&global_options);
    GltfCommand gltf_command(&global_options);
    TextureCommand texture_command(&global_options);
    std::vector<ImportCommand*> commands {
      &texture_command,
      &fbx_command,
      &gltf_command,
      &batch_command,
    };

    const auto cli = BuildCli(commands, global_options);
    const auto context = cli->Parse(argc, const_cast<const char**>(argv));

    if (context.global_option_groups != nullptr) {
      for (const auto& group : *context.global_option_groups) {
        for (const auto& option : *group.first) {
          option->FinalizeValue(context.ovm);
        }
      }
    }

    ApplyLoggingOptions(global_options);

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
