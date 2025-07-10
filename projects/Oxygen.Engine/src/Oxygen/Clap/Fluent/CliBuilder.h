//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <utility>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap {

/*!
 * \brief Fluent builder to properly create and configure a `Cli`.
 *
 * ### Design notes
 *
 * - This builder is facets-compatible, which means that it can be extended with
 *   additional facets implemented as derived classes constructed with the same
 *   encapsulated object.
 *
 * - Two styles are supported to get the Cli instance out of the builder: by
 *   explicitly calling the `Build()` method, or with an implicit conversion to
 *   a `std::unique_ptr<Cli>`.
 */
class CliBuilder {
  using Self = CliBuilder;

public:
  explicit CliBuilder()
    : cli_(new Cli())
  {
  }

  /// Set the program version string.
  OXGN_CLP_API auto Version(std::string version) -> Self&;

  /// Set the program name when it is preferred to use a customer name rather
  /// than the one coming as part of the command line arguments array.
  OXGN_CLP_API auto ProgramName(std::string name) -> Self&;

  /// Set the descriptive message about this command line program.
  OXGN_CLP_API auto About(std::string about) -> Self&;

  /// Add the given command to the `Cli`.
  OXGN_CLP_API auto WithCommand(std::shared_ptr<Command> command) -> Self&;

  /**
   * \brief Enable the default handling for the version option/command.
   *
   * With this, version information can be displayed using one of the following
   *
   * methods:
   *   - `program version`
   *   - `program --version`
   *   - `program -v`
   */
  OXGN_CLP_API auto WithVersionCommand() -> Self&;

  /**
   * Enable the default handling for the help option/command.
   *
   * With this, version general CLI help can be displayed using one of the
   * following methods:
   *  - `program help`
   *  - `program --help`
   *  - `program -h`
   *
   * Help for a specific command can be displayed using:
   *  - `program help command`
   *  - `program command --help`
   *  - `program command -h`
   */
  OXGN_CLP_API auto WithHelpCommand() -> Self&;

  /// Explicitly get the encapsulated `Cli` instance.
  OXGN_CLP_API auto Build() -> std::unique_ptr<Cli>;

  /// Automatic conversion to `Cli` smart pointer rendering the final call to
  /// Build() unnecessary.
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  operator std::unique_ptr<Cli>() { return Build(); }

protected:
  // We use a `unique_ptr` here instead of a simple contained object in order to
  // have the encapsulated object initialized at construction of the builder,
  // and then once moved out, the builder becomes explicitly invalid.

  // This member is protected to allow implementation of builder facets.
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<Cli> cli_;

  // This protected constructor is used by builder facets to share the same
  // encapsulated object between this builder and its facets.
  explicit CliBuilder(std::unique_ptr<Cli> cli)
    : cli_ { std::move(cli) }
  {
  }

private:
  auto AddHelpOptionToCommand(Command& command) const -> void;
  auto AddVersionOptionToCommand(Command& command) const -> void;
};

} // namespace oxygen::clap
