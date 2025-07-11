//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap {

/*!
 * \brief Fluent builder to properly create and configure a `Command`.
 *
 * ### Design notes
 *
 * - This builder is facets-compatible, which means that it can be extended with
 *   additional facets implemented as derived classes constructed with the same
 *   encapsulated object.
 *
 * - Two styles are supported to get the Command instance out of the builder: by
 *   explicitly calling the `Build()` method, or with an implicit conversion to
 *   a `std::unique_ptr<Command>`.
 */
class CommandBuilder {
  using Self = CommandBuilder;

public:
  template <typename... Segment>
  explicit CommandBuilder(std::string first_segment, Segment... other_segments)
    : command_(
        new Command(std::move(first_segment), std::move(other_segments)...))
  {
  }

  /// Set the descriptive message about this command line program.
  auto About(std::string about) -> Self&
  {
    if (!command_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    command_->About(std::move(about));
    return *this;
  }

  auto WithOptions(std::shared_ptr<Options> options, bool hidden = false)
    -> Self&
  {
    if (!command_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    command_->WithOptions(std::move(options), hidden);
    return *this;
  }

  auto WithOption(std::shared_ptr<Option> option) -> Self&
  {
    if (!command_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    command_->WithOption(std::move(option));
    return *this;
  }

  template <typename... Args>
  auto WithPositionalArguments(Args&&... options) -> Self&
  {
    command_->WithPositionalArguments(std::forward<Args>(options)...);
    return *this;
  }

  //! Explicitly get the encapsulated `Command` instance.
  auto Build() -> std::unique_ptr<Command> { return std::move(command_); }

  //! Automatic conversion to `Command` smart pointer rendering the final call
  //! to Build() unnecessary.
  operator std::unique_ptr<Command>() { return Build(); }

  //! Automatic conversion to `Command` smart pointer rendering the final call
  //! to Build() unnecessary.
  operator std::shared_ptr<Command>()
  {
    return std::shared_ptr { std::move(command_) };
  }

protected:
  // We use a `unique_ptr` here instead of a simple contained object in order to
  // have the encapsulated object initialized at construction of the builder,
  // and then once moved out, the builder becomes explicitly invalid.

  // This member is protected to allow implementation of builder facets.
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<Command> command_;

  // This protected constructor is used by builder facets to share the same
  // encapsulated object between this builder and its facets.
  explicit CommandBuilder(std::unique_ptr<Command> cli)
    : command_ { std::move(cli) }
  {
  }
};

} // namespace oxygen::clap
