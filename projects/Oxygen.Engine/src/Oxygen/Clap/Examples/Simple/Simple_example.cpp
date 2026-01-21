//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>

using oxygen::clap::Cli;
using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;

auto main(const int argc, char** argv) -> int
{
  try {
    bool quiet { false };

    // Describe the `default` command for this program.
    // We could also use a specific command by providing a specific name
    // when creating the command.

    CommandBuilder command_builder(Command::DEFAULT);
    //! [SimpleOptionFlag example]
    command_builder.WithOption(
      // Define a boolean flag option to configure `quiet` mode for
      // the program
      Option::WithKey("quiet")
        .About("Don't print anything to the standard output.")
        .Short("q")
        .Long("quiet")
        .WithValue<bool>()
        .StoreTo(&quiet)
        .Build());

    //! [SimpleOptionFlag example]

    //! [ComplexOption example]
    constexpr int default_num_lines = 10;
    command_builder.WithOption(
      // Define an option to control a more sophisticated program
      // configuration parameter
      Option::WithKey("lines")
        .About("Print the first <num> lines instead of the first 10 (by "
               "default); with the leading '-', print all but the last "
               "<num> lines of each file.")
        .Short("n")
        .Long("lines")
        .WithValue<int>()
        .DefaultValue(default_num_lines)
        .UserFriendlyName("num")
        .Build());
    //! [ComplexOption example]

    const std::unique_ptr<Cli> cli
      = CliBuilder()
          .ProgramName("simple-cli")
          .Version("1.0.0")
          .About("This is a simple command line example to demonstrate the "
                 "commonly used features of `asap-clap`. It uses the "
                 "standard `version` and `help` commands and only "
                 "implements a default command with several options.")
          .WithAutoOutputWidth()
          .WithVersionCommand()
          .WithHelpCommand()
          .WithCommand(command_builder);

    const auto context = cli->Parse(argc, const_cast<const char**>(argv));
    const auto command_path = context.active_command->PathAsString();

    const auto& ovm = context.ovm;

    if (command_path == Command::VERSION || command_path == Command::HELP
      || ovm.HasOption(Command::HELP)) {
      return 0;
    }

    if (!quiet) {
      std::cout << "-- Simple command line invoked, value of `lines` is: "
                << ovm.ValuesOf("lines").at(0).GetAs<int>() << std::endl;
    }
    return EXIT_SUCCESS;
  } catch (...) {
    return EXIT_FAILURE;
  }
}
