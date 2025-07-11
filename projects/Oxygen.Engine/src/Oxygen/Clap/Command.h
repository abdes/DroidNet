//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Clap/Option.h>
#include <Oxygen/Clap/api_export.h>

/// Namespace for command line parsing related APIs.
namespace oxygen::clap {

struct CommandLineContext;

// Forward reference used to declare the weak pointer to the parent CLI.
class Cli;

/*!
 * \brief A command.
 */
class Command {
public:
  using Ptr = std::shared_ptr<Command>;

  /*!
   * \brief A helper to make it clearer when a command is created as the
   * *default* one (i.e. mounted at the root top-level).
   *
   * **Example**
   * \snippet command_test.cpp Default command
   */
  static constexpr auto DEFAULT = "";

  /// Version command name.
  static constexpr auto VERSION = "version";
  static constexpr auto VERSION_LONG = "--version";
  static constexpr auto VERSION_SHORT = "-v";

  /// Help command name.
  static constexpr auto HELP = "help";
  static constexpr auto HELP_LONG = "--help";
  static constexpr auto HELP_SHORT = "-h";

  Command(const Command& other) = delete;
  Command(Command&& other) noexcept = delete;
  auto operator=(const Command& other) -> Command& = delete;
  auto operator=(Command&& other) noexcept -> Command& = delete;

  ~Command() = default;

  /*!
   * \brief Check if this command is the default command (i.e. mounted at the
   * root top-level).
   *
   * The default command is a command which path has one and only one segment
   * and that segment is the empty string (`""`).
   *
   * \return `true` if this is the default command; `false` otherwise.
   */
  [[nodiscard]] auto IsDefault() const -> bool
  {
    return path_.size() == 1 && path_.front().empty();
  }

  /*!
   * \brief Returns a vector containing the segments in this command's path in
   * the order they need to appear on the command line.
   */
  [[nodiscard]] auto Path() const -> const std::vector<std::string>&
  {
    return path_;
  }

  /*!
   * \brief Returns a string containing a space separated list of this command's
   * path segments in the order they need to appear on the command line.
   */
  OXGN_CLP_NDAPI auto PathAsString() const -> std::string;

  [[nodiscard]] auto About() const -> const std::string& { return about_; }

  [[nodiscard]] auto FindShortOption(const std::string& name) const
    -> std::optional<std::shared_ptr<Option>>
  {
    const auto result = std::ranges::find_if(options_,
      [&name](const Option::Ptr& option) { return option->Short() == name; });
    if (result == options_.cend()) {
      return {};
    }
    return std::make_optional(*result);
  }

  [[nodiscard]] auto FindLongOption(const std::string& name) const
    -> std::optional<std::shared_ptr<Option>>
  {
    const auto result = std::ranges::find_if(options_,
      [&name](const Option::Ptr& option) { return option->Long() == name; });
    if (result == options_.cend()) {
      return {};
    }
    return std::make_optional(*result);
  }

  /** Outputs 'desc' to the specified stream, calling 'f' to output each
      option_description element. */
  OXGN_CLP_API auto Print(
    const CommandLineContext& context, unsigned width = 80) const -> void;

  OXGN_CLP_API auto PrintSynopsis(const CommandLineContext& context) const
    -> void;

  OXGN_CLP_API auto PrintOptions(
    const CommandLineContext& context, unsigned width) const -> void;

  [[nodiscard]] auto CommandOptions() const -> const std::vector<Option::Ptr>&
  {
    return options_;
  }

  [[nodiscard]] auto PositionalArguments() const
    -> const std::vector<Option::Ptr>&
  {
    return positional_args_;
  }

  friend class CommandBuilder;
  friend class CliBuilder; // to upgrade default command with help and version

private:
  /*!
   * \brief Construct a new Command object to be mounted at the path
   * corresponding to the provided segments.
   *
   * By default, a command is mounted at the top level, meaning that it starts
   * executing from the very first token in the command line arguments. This
   * corresponds to the typical command line programs that just do one specific
   * task and accept options to parametrize that task. This however does not fit
   * the scenario of command line tools that can execute multiple tasks (such as
   * `git` for example).
   *
   * To help with that, we support mounting commands at the specific path,
   * composed of one or more string segments. All the path segments of a command
   * must be matched in the order they are specified for the command to be
   * selected as a candidate.
   *
   * \tparam Segment we use a variadic template parameter pack for the path
   * segments, but we want all the segments to simply be strings. This is
   * achieved by forcing the first path segment type to string.
   *
   * \param first_segment first segment of that command path; here only to force
   * the parameter pack to only accept string.
   *
   * \param other_segments zero or more path segments; must all be of type
   * string.
   *
   * \throws std::domain_error when multiple path segments are provided and one
   * of them is `""` (empty string). The default command can only have one
   * segment that must be `""`.
   *
   * **Example**
   *
   * \snippet command_test.cpp Non-default command path
   *
   * \see DEFAULT
   */
  template <typename... Segment>
  explicit Command(std::string first_segment, Segment... other_segments)
    : path_ { std::move(first_segment), std::move(other_segments)... }
  {
    if ((std::ranges::find(path_, "") != std::end(path_))
      && (path_.size() != 1)) {
      throw std::domain_error(
        "default command can only have one path segment (an empty string)");
    }
  }

  auto About(std::string about) -> Command&
  {
    about_ = std::move(about);
    return *this;
  }

  OXGN_CLP_API auto WithOptions(std::shared_ptr<Options> options, bool hidden)
    -> void;

  OXGN_CLP_API auto WithOption(std::shared_ptr<Option>&& option) -> void;

  template <typename... Args>
  auto WithPositionalArguments(Args&&... options) -> void
  {
    positional_args_.insert(
      positional_args_.end(), { std::forward<Args>(options)... });
  }

  std::string about_;
  std::vector<std::string> path_;
  std::vector<Option::Ptr> options_;
  std::vector<bool> options_in_groups_;
  std::vector<std::pair<Options::Ptr, bool>> groups_;
  std::vector<Option::Ptr> positional_args_;

  // Only updated by the CliBuilder, and only used to refer back to the parent
  // CLI to get information for better help display. Use the helper methods
  // instead of directly accessing through the pointer for better
  // maintainability.
  Cli* parent_cli_ { nullptr };

  [[nodiscard]] auto ProgramName() const -> std::string;
};

} // namespace oxygen::clap
