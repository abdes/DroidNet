// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause.
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#pragma once

#include <iostream>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/OptionValuesMap.h>

namespace oxygen::clap {

// Forward declaration; see CliTheme.h for definition
struct CliTheme;

struct CommandLineContext {
  explicit CommandLineContext(std::string _program_name,
    Command::Ptr& active_command_ref, OptionValuesMap& ovm_ref,
    unsigned int output_width_value)
    : output_width { output_width_value }
    , program_name { std::move(_program_name) }
    , active_command { active_command_ref }
    , ovm { ovm_ref }
  {
  }

  const CliTheme* theme = nullptr;

  bool allow_long_option_value_with_no_equal { true };

  unsigned int output_width;

  std::istream& in { std::cin };
  std::ostream& out { std::cout };
  std::ostream& err { std::cerr };

  std::string program_name;

  /*!
   * \brief Tracks the `oxygen::clap::Command` objects for the active command.
   *
   * This field is populated with valid value as soon as the parser identifies a
   * valid command on the command line. All options during subsequent parsing
   * will be relative to this command.
   */
  Command::Ptr& active_command;

  OptionValuesMap& ovm;
};

} // namespace oxygen::clap
