//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <stdexcept>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/OptionBuilder.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>

#include <fmt/format.h>

auto oxygen::clap::CliBuilder::Version(std::string version) -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->Version(std::move(version));
  return *this;
}

auto oxygen::clap::CliBuilder::ProgramName(std::string name) -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->ProgramName(std::move(name));
  return *this;
}

auto oxygen::clap::CliBuilder::About(std::string about) -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->About(std::move(about));
  return *this;
}

auto oxygen::clap::CliBuilder::Footer(std::string footer) -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->Footer(std::move(footer));
  return *this;
}

auto oxygen::clap::CliBuilder::OutputWidth(const unsigned int width)
  -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  if (width < 1) {
    throw std::invalid_argument("OutputWidth must be >= 1");
  }
  cli_->OutputWidth(width);
  return *this;
}

auto oxygen::clap::CliBuilder::WithAutoOutputWidth() -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->EnableAutoOutputWidth();
  return *this;
}

auto oxygen::clap::CliBuilder::WithTheme(const CliTheme& theme) -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->Theme(theme);
  return *this;
}

auto oxygen::clap::CliBuilder::WithCommand(std::shared_ptr<Command> command)
  -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  if (!command) {
    throw std::invalid_argument("Command cannot be null");
  }
  cli_->WithCommand(std::move(command));
  return *this;
}

auto oxygen::clap::CliBuilder::WithGlobalOptions(
  std::shared_ptr<Options> options, const bool hidden) -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  if (!options) {
    throw std::invalid_argument("Options cannot be null");
  }
  cli_->WithGlobalOptions(std::move(options), hidden);
  return *this;
}

auto oxygen::clap::CliBuilder::WithGlobalOption(std::shared_ptr<Option> option)
  -> CliBuilder&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  if (!option) {
    throw std::invalid_argument("Option cannot be null");
  }
  cli_->WithGlobalOption(std::move(option));
  return *this;
}

auto oxygen::clap::CliBuilder::WithThemeSelectionOption() -> Self&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->EnableThemeSelectionOption();
  return *this;
}

auto oxygen::clap::CliBuilder::WithVersionCommand() -> Self&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->EnableVersionCommand();
  return *this;
}

auto oxygen::clap::CliBuilder::WithHelpCommand() -> Self&
{
  if (!cli_) {
    throw std::logic_error("OptionValueBuilder: method called after Build()");
  }
  cli_->EnableHelpCommand();
  return *this;
}

auto oxygen::clap::CliBuilder::AddHelpOptionToCommand(Command& command) const
  -> void
{
  command.WithOption(Option::WithKey("help")
      .About(fmt::format("Display detailed help information.\nNote "
                         "that `{} --help` is identical to `{} help` because "
                         "the former is internally converted into the latter.",
        cli_->ProgramName(), cli_->ProgramName(), cli_->ProgramName(),
        cli_->ProgramName()))
      .Short("h")
      .Long("help")
      .WithValue<bool>()
      .Build());
}

auto oxygen::clap::CliBuilder::AddVersionOptionToCommand(Command& command) const
  -> void
{
  command.WithOption(Option::WithKey("version")
      .About(fmt::format("Display version information.\nNote that `{} "
                         "--version` is identical to `{} version` because "
                         "the former is internally converted into the latter.",
        cli_->ProgramName(), cli_->ProgramName()))
      .Short("v")
      .Long("version")
      .WithValue<bool>()
      .Build());
}

auto oxygen::clap::CliBuilder::Build() -> std::unique_ptr<Cli>
{
  // Handle additional setup needed when the default `version` or `help`
  // commands are enabled.

  if (cli_->HasHelpCommand() || cli_->HasVersionCommand()) {
    auto has_default_command = false;

    for (auto& command : cli_->commands_) {
      if (cli_->HasHelpCommand()) {
        AddHelpOptionToCommand(*command);
      }
      if (command->IsDefault()) {
        has_default_command = true;
        if (cli_->HasVersionCommand()) {
          AddVersionOptionToCommand(*command);
        }
      }
    }

    // If the CLI did not have a default command, create one and set it up.
    if (!has_default_command) {
      const std::shared_ptr<Command> command = CommandBuilder(Command::DEFAULT);
      // = CommandBuilder(cli_->ProgramName(), Command::DEFAULT);
      if (cli_->HasHelpCommand()) {
        AddHelpOptionToCommand(*command);
      }
      if (cli_->HasVersionCommand()) {
        AddVersionOptionToCommand(*command);
      }
      WithCommand(command);
    }
  }

  // Update all CLI commands to have a weak reference to the parent CLI
  for (const auto& command : cli_->commands_) {
    command->parent_cli_ = cli_.get();
  }

  return std::move(cli_);
}
