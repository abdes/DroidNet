//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/TextWrap/TextWrap.h>

auto oxygen::clap::Command::ProgramName() const -> std::string
{
  return parent_cli_ ? parent_cli_->ProgramName() : "<program>";
}

void oxygen::clap::Command::PrintSynopsis(std::ostream& out) const
{
  out << ProgramName() << " " << PathAsString() << " ";
  for (const auto& option : options_) {
    out << (option->value_semantic()->IsRequired() ? "" : "[");
    if (!option->Short().empty()) {
      out << "-" << option->Short();
      if (!option->Long().empty()) {
        out << ",";
      }
    }
    if (!option->Long().empty()) {
      out << "--" << option->Long();
    }
    out << (option->value_semantic()->IsRequired() ? " " : "] ");
  }

  for (const auto& option : positional_args_) {
    if (option->IsPositionalRest()) {
      if (!option->IsRequired()) {
        out << "[";
      }
      out << "<" << option->UserFriendlyName() << ">";
      if (!option->IsRequired()) {
        out << "]";
      }
    } else {
      out << " " << (option->value_semantic()->IsRequired() ? "" : "[")
          << option->Key()
          << (option->value_semantic()->IsRequired() ? "" : "]");
    }
  }
}

void oxygen::clap::Command::PrintOptions(
  std::ostream& out, unsigned int width) const
{

  for (unsigned option_index = 0; option_index < options_.size();
    ++option_index) {
    if (options_in_groups_[option_index]) {
      continue;
    }
    options_[option_index]->Print(out, width);
    out << "\n\n";
  }

  for (auto [group, hidden] : groups_) {
    if (!hidden) {
      group->Print(out, width);
      out << "\n\n";
    }
  }

  for (const auto& positional : positional_args_) {
    positional->Print(out, width);
    out << "\n\n";
  }
}

void oxygen::clap::Command::Print(std::ostream& out, unsigned int width) const
{
  wrap::TextWrapper wrap = wrap::MakeWrapper()
                             .Width(width)
                             .CollapseWhiteSpace()
                             .TrimLines()
                             .IndentWith()
                             .Initially("   ")
                             .Then("   ");
  std::ostringstream ostr;

  out << "SYNOPSIS\n";
  PrintSynopsis(ostr);
  out << wrap.Fill(ostr.str()).value();
  ostr.str("");
  ostr.clear();
  out << "\n\n";

  out << "DESCRIPTION\n";
  if (PathAsString() == Command::DEFAULT) {
    out << wrap.Fill(parent_cli_->About()).value();
  } else {
    out << wrap.Fill(about_).value();
  }
  out << "\n\n";

  out << "OPTIONS\n";
  PrintOptions(out, width);
}

auto oxygen::clap::Command::PathAsString() const -> std::string
{
  return fmt::format("{}", fmt::join(path_, " "));
}
