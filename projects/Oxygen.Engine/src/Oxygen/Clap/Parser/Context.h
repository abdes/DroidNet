// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause.
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#pragma once

#include <memory>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>

namespace oxygen::clap::parser::detail {

using CommandPtr = Command::Ptr;
using CommandsList = std::vector<CommandPtr>;
using OptionPtr = Option::Ptr;

/*!
 * \brief Encapsulates data needed or produced by the command line arguments
 * parser during its lifetime.
 *
 * When a command line parser is started, an instance of this `ParserContext`
 * class is created and passed to its state machine's initial state. This
 * context is then used for the lifetime of the state machine to share data and
 * results between states through the actions produced by events being handled
 * by the states.
 *
 * Each state will explicitly document its expectations in terms of data
 * required to be present in the context and data it updates itself.
 *
 * \see States.h
 */
struct ParserContext : CommandLineContext {
  /*!
   * \brief An alias for a smart pointer to a parser context.
   *
   * \note The `ParserContext` is intended to be exclusively used through a
   * shared smart pointer. Hence, its constructors are private and to make a new
   * `ParserContext` one must use its `New()` factory method.
   */
  using Ptr = std::shared_ptr<ParserContext>;

  /*!
   * \brief Create an instance of the `ParserContext` class, initialized with
   * the given list of commands.
   *
   * For a particular command line parser instance, one `ParserContext` used
   * from start to finish. Only one instance is created at start, which then
   * gets passed from state to state via action data.
   *
   * \param base the base CLI context that this parser context would use to
   * initialize its base class data.
   * \param cli_commands the list of commands
   * supported by the CLI. \return a shared smart pointer to a new instance of
   * `ParserContext`.
   */
  static auto New(
    const CommandLineContext& base, const CommandsList& cli_commands) -> Ptr
  {
    return std::shared_ptr<ParserContext>(
      new ParserContext(base, cli_commands));
  }

  /*!
   * \brief The list of all known command for this CLI, including the `default`
   * command if one has been specified.
   *
   * This field is always populated when the parser context is created and
   * remains valid for its lifetime.
   */
  const CommandsList& commands;
  /*!
   * \brief Tracks the `oxygen::clap::Option` object for the command line option
   * currently being parsed.
   *
   * This field is updated every time a command line argument is identified as a
   * known option (short name, long name, lone dash or double dash).
   */
  OptionPtr active_option;
  /*!
   * \brief Tracks the flag (including the '-' or '--' for long options)
   * corresponding to the command line option currently being parsed.
   *
   * This field is mainly used for diagnostic messages. For the current option
   * being parsed, refer to the `active_option` field.
   */
  std::string active_option_flag;

  /*! \brief Value tokens collected while the parser is matching commands and
   * options which do not correspond to a command path segment or an option
   * value argument.
   *
   * These positional tokens will be processed all together in the order they
   * were encountered once the command line options parsing is complete.
   *
   * \see FinalState
   */
  std::vector<std::string> positional_tokens;

private:
  // Constructor is private. Use `New()` to create an instance of this class.
  explicit ParserContext(
    const CommandLineContext& base, const CommandsList& cli_commands)
    : CommandLineContext(base)
    , commands { cli_commands }
  {
  }
};
using ParserContextPtr = std::shared_ptr<ParserContext>;

} // namespace oxygen::clap::parser::detail
