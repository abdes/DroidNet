//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <fmt/ranges.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CliTheme.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Internal/StyledWrap.h>

auto oxygen::clap::Command::ProgramName() const -> std::string
{
  return parent_cli_ ? parent_cli_->ProgramName() : "<program>";
}

auto oxygen::clap::Command::PrintSynopsis(
  const CommandLineContext& context) const -> void
{
  context.out << ProgramName() << " " << PathAsString() << " ";
  for (const auto& option : options_) {
    context.out << (option->value_semantic()->IsRequired() ? "" : "[");
    if (!option->Short().empty()) {
      context.out << "-" << option->Short();
      if (!option->Long().empty()) {
        context.out << ",";
      }
    }
    if (!option->Long().empty()) {
      context.out << "--" << option->Long();
    }
    context.out << (option->value_semantic()->IsRequired() ? " " : "] ");
  }

  for (const auto& option : positional_args_) {
    if (option->IsPositionalRest()) {
      if (!option->IsRequired()) {
        context.out << "[";
      }
      context.out << "<" << option->UserFriendlyName() << ">";
      if (!option->IsRequired()) {
        context.out << "]";
      }
    } else {
      context.out << " " << (option->value_semantic()->IsRequired() ? "" : "[")
                  << option->Key()
                  << (option->value_semantic()->IsRequired() ? "" : "]");
    }
  }
}

auto oxygen::clap::Command::PrintOptions(
  const CommandLineContext& context, unsigned int width) const -> void
{
  for (unsigned option_index = 0; option_index < options_.size();
    ++option_index) {
    if (options_in_groups_[option_index]) {
      continue;
    }
    options_[option_index]->Print(context, width);
    context.out << "\n\n";
  }

  for (auto [group, hidden] : groups_) {
    if (!hidden) {
      group->Print(context, width);
      context.out << "\n\n";
    }
  }

  for (const auto& positional : positional_args_) {
    positional->Print(context, width);
    context.out << "\n\n";
  }
}

auto oxygen::clap::Command::WithOptions(
  std::shared_ptr<Options> options, bool hidden) -> void
{
  DCHECK_NOTNULL_F(options);
  for (const auto& option : *options) {
    options_.push_back(option);
    options_in_groups_.push_back(true);
  }
  groups_.emplace_back(std::move(options), hidden);
}

auto oxygen::clap::Command::WithOption(std::shared_ptr<Option>&& option) -> void
{
  DCHECK_NOTNULL_F(option);
  if (option->Key() == HELP || option->Key() == VERSION) {
    options_.emplace(options_.begin(), option);
    options_in_groups_.insert(options_in_groups_.begin(), false);
  } else {
    options_.push_back(std::move(option));
    options_in_groups_.push_back(false);
  }
}

auto oxygen::clap::Command::Print(
  const CommandLineContext& context, unsigned int width) const -> void
{
  wrap::TextWrapper wrap = detail::MakeStyledWrapper(width, "   ", "   ");
  std::ostringstream ostr;

  const CliTheme& theme = context.theme ? *context.theme : CliTheme::Plain();
  context.out << fmt::format(theme.section_header, "SYNOPSIS\n");
  PrintSynopsis(context);
  context.out << wrap.Fill(ostr.str()).value();
  ostr.str("");
  ostr.clear();
  context.out << "\n\n";

  context.out << fmt::format(theme.section_header, "DESCRIPTION\n");
  if (PathAsString() == DEFAULT) {
    context.out << wrap.Fill(parent_cli_->About()).value();
  } else {
    context.out << wrap.Fill(about_).value();
  }
  context.out << "\n\n";

  bool has_visible_globals = false;
  if (context.global_option_groups && !context.global_option_groups->empty()) {
    for (const auto& [group, hidden] : *context.global_option_groups) {
      if (!hidden && group) {
        has_visible_globals = true;
        break;
      }
    }
  }

  if (has_visible_globals) {
    context.out << fmt::format(theme.section_header, "GLOBAL OPTIONS\n")
                << "\n";
    for (const auto& [group, hidden] : *context.global_option_groups) {
      if (!hidden && group) {
        group->Print(context, width);
        context.out << "\n\n";
      }
    }
  }

  bool has_visible_command_options = false;
  for (unsigned option_index = 0; option_index < options_.size();
    ++option_index) {
    if (!options_in_groups_[option_index]) {
      has_visible_command_options = true;
      break;
    }
  }
  if (!has_visible_command_options) {
    for (const auto& [group, hidden] : groups_) {
      if (!hidden && group) {
        has_visible_command_options = true;
        break;
      }
    }
  }
  if (!has_visible_command_options) {
    has_visible_command_options = !positional_args_.empty();
  }

  if (has_visible_command_options) {
    context.out << fmt::format(theme.section_header, "OPTIONS\n") << "\n";
    PrintOptions(context, width);
  }
}

auto oxygen::clap::Command::PathAsString() const -> std::string
{
  return fmt::format("{}", fmt::join(path_, " "));
}
