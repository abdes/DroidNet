//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <fmt/ranges.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CliTheme.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/PositionalOptionBuilder.h>
#include <Oxygen/Clap/Internal/Args.h>
#include <Oxygen/Clap/Parser/Parser.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>
#include <Oxygen/TextWrap/TextWrap.h>

using oxygen::clap::detail::Arguments;

namespace oxygen::clap {

CmdLineArgumentsError::~CmdLineArgumentsError() = default;

auto Cli::Parse(const int argc, const char** argv) -> CommandLineContext
{
  const Arguments cla { argc, argv };

  if (!program_name_) {
    program_name_ = cla.ProgramName();
  }

  auto args = cla.Args();

  // Simplify processing by transforming the shor or long option forms of
  // `version` and `help` into the corresponding unified command name.
  if (!args.empty()) {
    std::string first = args[0];
    if (has_version_command_
      && (first == Command::VERSION_SHORT || first == Command::VERSION_LONG)) {
      first.assign(Command::VERSION);
    } else if (has_help_command_
      && (first == Command::HELP_SHORT || first == Command::HELP_LONG)) {
      first.assign(Command::HELP);
    }
  }

  const parser::Tokenizer tokenizer { cla.Args() };
  CommandLineContext context(ProgramName(), active_command_, ovm_);
  context.theme = &CliTheme::Dark(); // Set a default theme
  parser::CmdLineParser parser(context, tokenizer, commands_);
  if (parser.Parse()) {
    // Check if we need to handle a `version` or `help` command
    if (context.active_command->PathAsString() == "help"
      || context.ovm.HasOption("help")) {
      HandleHelpCommand(context);
    } else if (context.active_command->PathAsString() == "version") {
      HandleVersionCommand(context);
    }

    return context;
  }
  if (HasHelpCommand()) {
    context.out << fmt::format(
      "Try '{} --help' for more information.", program_name_.value())
                << '\n';
  }
  throw CmdLineArgumentsError(
    fmt::format("command line arguments parsing failed, try '{} --help' for "
                "more information.",
      program_name_.value()));
}

auto Cli::PrintDefaultCommand(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  const auto default_command = std::ranges::find_if(
    commands_, [](const auto& command) { return command->IsDefault(); });
  if (default_command != commands_.end()) {
    (*default_command)->Print(context, width);
  }
}

auto Cli::PrintCommands(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  const CliTheme& theme = context.theme ? *context.theme : CliTheme::Plain();
  context.out << fmt::format(theme.section_header, "SUB-COMMANDS\n\n");
  for (const auto& command : commands_) {
    if (!command->IsDefault()) {
      context.out << "   "
                  << fmt::format(
                       theme.command_name, "{}", command->PathAsString())
                  << "\n";
      wrap::TextWrapper wrap = wrap::MakeWrapper()
                                 .Width(width)
                                 .TrimLines()
                                 .IndentWith()
                                 .Initially("     ")
                                 .Then("     ");
      context.out << wrap.Fill(command->About()).value_or("__wrapping_error__");
      context.out << "\n\n";
    }
  }
}

auto Cli::Print(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  PrintDefaultCommand(context, width);
  PrintCommands(context, width);
}

auto Cli::EnableVersionCommand() -> void
{
  const Command::Ptr command { CommandBuilder(Command::VERSION)
      .About(fmt::format(
        "Display version information.", ProgramName(), ProgramName())) };
  commands_.push_back(command);
  has_version_command_ = true;
}

auto Cli::HandleVersionCommand(const CommandLineContext& context) const -> void
{
  context.out << fmt::format(
    "{} version {}\n", program_name_.value_or("__no_program_name__"), version_)
              << '\n';
}

auto Cli::EnableHelpCommand() -> void
{
  const Command::Ptr command { CommandBuilder(Command::HELP)
      .About(fmt::format("Display detailed help information. `{} help` lists "
                         "available sub-commands and a summary of what they "
                         "do. See `{} help <command>` to get detailed help "
                         "for a specific sub-command.",
        ProgramName(), ProgramName()))
      .WithPositionalArguments(Option::Rest()
          .UserFriendlyName("SEGMENTS")
          .About("The path segments (in the correct order) of the "
                 "sub-command for which help information should be "
                 "displayed.")
          .WithValue<std::string>()
          .Build()) };
  commands_.push_back(command);
  has_help_command_ = true;
}
auto Cli::HandleHelpCommand(const CommandLineContext& context) const -> void
{
  if (context.ovm.HasOption("help")) {
    context.active_command->Print(context, 80);
  } else if (context.active_command->PathAsString() == "help") {
    if (context.ovm.HasOption(Option::key_rest_)) {
      const auto& values = context.ovm.ValuesOf(Option::key_rest_);
      DCHECK_F(!values.empty());
      std::vector<std::string> command_path;
      command_path.reserve(values.size());
      for (const auto& value : values) {
        command_path.push_back(value.GetAs<std::string>());
      }

      auto command = std::ranges::find_if(
        commands_, [&command_path](const Command::Ptr& cmd) {
          return cmd->Path() == command_path;
        });
      if (command != commands_.end()) {
        (*command)->Print(context, 80);
      } else {
        context.err << fmt::format(
          "The path `{}` does not correspond to a known command.\n",
          fmt::join(command_path, " "));
        context.out << fmt::format("Try '{} --help' for more information.",
          program_name_.value_or("__no_program_name__"))
                    << '\n';
      }
    } else {
      PrintDefaultCommand(context, 80);
      PrintCommands(context, 80);
    }
  }
}
} // namespace oxygen::clap
