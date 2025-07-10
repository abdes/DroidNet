//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/PositionalOptionBuilder.h>
#include <Oxygen/Clap/Internal/Args.h>
#include <Oxygen/Clap/Parser/Parser.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>
#include <Oxygen/TextWrap/TextWrap.h>

using asap::clap::detail::Arguments;

namespace asap::clap {

CmdLineArgumentsError::~CmdLineArgumentsError() = default;

auto Cli::Parse(int argc, const char** argv) -> CommandLineContext
{
  const Arguments cla { argc, argv };

  if (!program_name_) {
    program_name_ = cla.ProgramName();
  }

  auto& args = cla.Args();

  // Simplify processing by transforming the shor or long option forms of
  // `version` and `help` into the corresponding unified command name.
  if (!args.empty()) {
    std::string& first = args[0];
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
    context.out_ << fmt::format(
      "Try '{} --help' for more information.", program_name_.value())
                 << std::endl;
  }
  throw CmdLineArgumentsError(
    fmt::format("command line arguments parsing failed, try '{} --help' for "
                "more information.",
      program_name_.value()));
}

auto operator<<(std::ostream& out, const Cli& cli) -> std::ostream&
{
  cli.Print(out);
  return out;
}

void Cli::PrintDefaultCommand(std::ostream& out, unsigned int width) const
{
  const auto default_command = std::find_if(commands_.begin(), commands_.end(),
    [](const auto& command) { return command->IsDefault(); });
  if (default_command != commands_.end()) {
    (*default_command)->Print(out, width);
  }
}

void Cli::PrintCommands(std::ostream& out, unsigned int width) const
{
  out << "SUB-COMMANDS\n\n";
  for (const auto& command : commands_) {
    if (!command->IsDefault()) {
      out << "   " << command->PathAsString() << "\n";
      wrap::TextWrapper wrap = wrap::MakeWrapper()
                                 .Width(width)
                                 .TrimLines()
                                 .IndentWith()
                                 .Initially("     ")
                                 .Then("     ");
      out << wrap.Fill(command->About()).value();
      out << "\n\n";
    }
  }
}

void Cli::Print(std::ostream& out, unsigned int width) const
{
  PrintDefaultCommand(out, width);
  PrintCommands(out, width);
}

void Cli::EnableVersionCommand()
{
  const Command::Ptr command { CommandBuilder(Command::VERSION)
      .About(fmt::format(
        "Display version information.", ProgramName(), ProgramName())) };
  commands_.push_back(command);
  has_version_command_ = true;
}

void Cli::HandleVersionCommand(const CommandLineContext& context) const
{
  context.out_ << fmt::format("{} version {}\n", program_name_.value(),
    version_) << std::endl;
}

void Cli::EnableHelpCommand()
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
void Cli::HandleHelpCommand(const CommandLineContext& context) const
{
  if (context.ovm.HasOption("help")) {
    context.active_command->Print(context.out_, 80);
  } else if (context.active_command->PathAsString() == "help") {
    if (context.ovm.HasOption(Option::key_rest)) {
      const auto& values = context.ovm.ValuesOf(Option::key_rest);
      DCHECK_F(!values.empty());
      std::vector<std::string> command_path;
      command_path.reserve(values.size());
      for (const auto& value : values) {
        command_path.push_back(value.GetAs<std::string>());
      }

      const auto& command = std::find_if(commands_.begin(), commands_.end(),
        [&command_path](const Command::Ptr& command) {
          return command->Path() == command_path;
        });
      if (command != commands_.end()) {
        (*command)->Print(context.out_, 80);
      } else {
        context.err_ << fmt::format(
          "The path `{}` does not correspond to a known command.\n",
          fmt::join(command_path, " "));
        context.out_ << fmt::format(
          "Try '{} --help' for more information.", program_name_.value())
                     << std::endl;
      }
    } else {
      PrintDefaultCommand(context.out_, 80);
      PrintCommands(context.out_, 80);
    }
  }
}
} // namespace asap::clap
