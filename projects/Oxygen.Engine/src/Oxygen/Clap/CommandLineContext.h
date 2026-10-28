// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause.
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#pragma once

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/OptionValuesMap.h>

namespace oxygen::clap {

// Forward declaration; see CliTheme.h for definition
struct CliTheme;

//! Where a parse writes its help, version and error text; by default, the
//! process standard output and error streams.
struct CliStreams {
  observer_ptr<std::ostream> out { &std::cout };
  observer_ptr<std::ostream> err { &std::cerr };
};

//! One parse of a command line: its settings, and what it found.
/*!
 The context owns what the parse finds, the active command and the option
 values, so it can outlive the parser that filled it.
*/
struct CommandLineContext {
  explicit CommandLineContext(std::string _program_name,
    Command::Ptr initial_command, const unsigned int output_width_value,
    const CliStreams streams = {})
    : output_width { output_width_value }
    , out { streams.out }
    , err { streams.err }
    , program_name { std::move(_program_name) }
    , active_command { std::move(initial_command) }
  {
  }

  const CliTheme* theme = nullptr;

  bool allow_long_option_value_with_no_equal { true };

  unsigned int output_width;

  const std::vector<std::pair<Options::Ptr, bool>>* global_option_groups
    = nullptr;

  //! The stream help and version text go to.
  observer_ptr<std::ostream> out;
  //! The stream parse errors go to.
  observer_ptr<std::ostream> err;

  std::string program_name;

  /*!
   * \brief Tracks the `oxygen::clap::Command` objects for the active command.
   *
   * This field is populated with valid value as soon as the parser identifies a
   * valid command on the command line. All options during subsequent parsing
   * will be relative to this command.
   */
  Command::Ptr active_command;

  //! The values of the options the command line set, or that have defaults.
  OptionValuesMap ovm;
};

} // namespace oxygen::clap
