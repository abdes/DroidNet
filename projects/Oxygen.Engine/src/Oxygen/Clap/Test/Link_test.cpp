//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Clap/Parser/Parser.h>

using namespace oxygen::clap;

#include <Oxygen/Base/Logging.h>

auto main(int argc, char** argv) -> int
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_header = false;
  loguru::g_stderr_verbosity = loguru::Verbosity_INFO;

  const auto common_options = std::make_shared<Options>("Common options");

  common_options->Add(Option::WithKey("verbose")
      .Short("v")
      .Long("verbose")
      .WithValue<bool>()
      .Build());

  const Command::Ptr default_command { CommandBuilder(Command::DEFAULT)
      .WithOptions(common_options)
      .WithOption(Option::WithKey("INPUT")
          .About("The input file")
          .WithValue<std::string>()
          .Build())
      .Build() };

  const Command::Ptr just_command { CommandBuilder("just", "hello")
      .WithOptions(common_options, /* hidden */ true)
      .WithOption(Option::WithKey("first_opt")
          .About("The first option")
          .Short("f")
          .Long("first-option")
          .WithValue<unsigned>()
          .DefaultValue(1)
          .ImplicitValue(1)
          .Build())
      .WithOption(Option::WithKey("second_opt")
          .About("The second option")
          .Short("s")
          .Long("second-option")
          .WithValue<std::string>()
          .DefaultValue("1")
          .ImplicitValue("1")
          .Build())
      .Build() };

  const Command::Ptr doit_command { CommandBuilder("just", "do", "it")
      .WithOption(Option::WithKey("third_opt")
          .About("The third option")
          .Short("t")
          .Long("third-option")
          .WithValue<unsigned>()
          .Build())
      .Build() };

  std::unique_ptr<Cli> cli
    = CliBuilder()
        .ProgramName("LinkTest")
        .Version("1.0.0")
        .About("This is a simple command line example to demonstrate the "
               "commonly used features of `asap-clap`. It uses the "
               "standard `version` and `help` commands and only "
               "implements a default command with several options.")
        .WithVersionCommand()
        .WithHelpCommand()
        .WithCommand(default_command)
        .WithCommand(just_command)
        .WithCommand(doit_command);

  if (!cli) {
    std::cerr << "Failed to create CLI instance.\n";
    return EXIT_FAILURE;
  }

  const auto context = cli->Parse(argc, const_cast<const char**>(argv));

  return EXIT_SUCCESS;
}
