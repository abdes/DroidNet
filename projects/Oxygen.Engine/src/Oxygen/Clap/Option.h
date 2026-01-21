//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Clap/Detail/ParseValue.h>
#include <Oxygen/Clap/ValueSemantics.h>
#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap {

struct CommandLineContext;
class OptionBuilder;

template <typename Builder,
  std::enable_if_t<std::is_same_v<Builder, OptionBuilder>, std::nullptr_t>
  = nullptr>
class Positional;
/// A type alias for the positional option builder template.
using PositionalOptionBuilder = Positional<OptionBuilder>;

/**
 * \brief Describes a command line option (aka argument).
 *
 * An option on the command line can be specified with its short form (e.g.
 * '-v') or its long form (e.g. '--verbose') and can optionally have a value.
 * The value is added right next to the option separated by a character ' ' or
 * '=' depending on whether the short or the long form was used.
 *
 * Command line options come in multiple forms:
 *
 * - Pure flag options, which do not require an explicit value and instead take
 *   an implicit value when present on the command line and a default value when
 *   not present. A typical example is a boolean flag option which takes the
 *   value of true when present on the command line and false when not.
 *
 * - With user-provided values, which constitute the bulk of command line
 *   options in usual programs. Such options can specify additional constraints
 *   on the value semantics, such as whether the value is optional or required,
 *   can be repeated multiple times or not, etc.
 *
 * - Positional options. These are options present on the command line without a
 *   leading '-' character and usually correspond to input arguments to the
 *   program rather than configuration options. A program can have one or many
 *   of such input arguments, which are identified by their position on the
 *   command line. Order of appearance decides which option we are dealing with,
 *   therefore the `positional` in the definition.
 *
 * In certain command lines, there may be more remaining unnamed arguments after
 * the parsing is complete. Such remaining arguments is often valid and is
 * interpreted as a catchall type of input to the program for the `Rest` of what
 * remains on the command line. In this API, it is identified with the special
 * key Option::key_rest ('_REST_').
 *
 * Command line options can be described easily using the fluent API provided by
 * the OptionBuilder, its Positional variant and the associated
 * OptionValueBuilder classes.
 *
 * \section examples Examples
 *
 * \subsection boolean-flag Boolean flag option \snippet simple_example.cpp
 * SimpleOptionFlag example
 *
 * \subsection complex-option Option with complex value semantics \snippet
 * simple_example.cpp ComplexOption example
 */
class Option {
public:
  constexpr static auto key_rest_ = "_REST_";

  using Ptr = std::shared_ptr<Option>;

  [[nodiscard]] auto Short() const -> const std::string& { return short_name_; }

  auto Short(std::string short_name) -> void
  {
    short_name_ = std::move(short_name);
  }

  [[nodiscard]] auto Long() const -> const std::string& { return long_name_; }

  auto Long(std::string long_name) -> void
  {
    long_name_ = std::move(long_name);
  }

  [[nodiscard]] auto About() const -> const std::string& { return about_; }

  auto About(std::string about) -> void { about_ = std::move(about); }

  [[nodiscard]] auto UserFriendlyName() const -> const std::string&
  {
    return (!user_friendly_name_.empty()) ? user_friendly_name_ : key_;
  }

  auto UserFriendlyName(std::string name) -> void
  {
    user_friendly_name_ = std::move(name);
  }

  [[nodiscard]] auto Key() const -> const std::string& { return key_; }

  auto Required() -> void { required_ = true; }

  [[nodiscard]] auto IsRequired() const -> bool { return required_; }

  [[nodiscard]] auto IsPositional() const -> bool
  {
    return long_name_.empty() && short_name_.empty();
  }

  [[nodiscard]] auto IsPositionalRest() const -> bool
  {
    return IsPositional() && key_ == key_rest_;
  }

  /** Outputs 'desc' to the specified stream, calling 'f' to output each
      option_description element. */
  OXGN_CLP_API auto Print(
    const CommandLineContext& context, unsigned width = 0) const -> void;

  static OXGN_CLP_API auto WithKey(std::string key) -> OptionBuilder;

  static OXGN_CLP_API auto Positional(std::string key)
    -> PositionalOptionBuilder;

  static OXGN_CLP_API auto Rest() -> PositionalOptionBuilder;

  /// Semantic of option's value
  [[nodiscard]] auto value_semantic() const
    -> std::shared_ptr<const ValueSemantics>
  {
    return value_semantic_;
  }

  friend class OptionBuilder;

  template <typename> friend class OptionValueBuilder;

  //! Finalize and propagate parsed (or default) value to user storage.
  OXGN_CLP_API auto FinalizeValue(const class OptionValuesMap& ovm) const
    -> void;

private:
  std::string key_;
  std::string short_name_;
  std::string long_name_;
  std::string about_;
  std::string user_friendly_name_;
  bool required_ { false };

  // shared_ptr is needed to simplify memory management in copy ctor and
  // destructor.
  std::shared_ptr<ValueSemantics> value_semantic_;

  explicit Option(std::string key)
    : key_(std::move(key))
  {
  }

  template <typename T>
  Option(std::string key, std::unique_ptr<ValueSemantics> value_semantic)
    : key_(std::move(key))
    , value_semantic_(std::move(value_semantic))
  {
  }

  auto PrintValueDescription(const CommandLineContext& context,
    const std::string& separator) const -> void;
};

struct Options {
  using Ptr = std::shared_ptr<Options>;
  using OptionsCollectionType = std::vector<std::shared_ptr<Option>>;

  explicit Options(std::string _label)
    : label { std::move(_label) }
  {
  }

  /** Adds new variable description. Throws duplicate_variable_error if
      either short or long name matches that of already present one.
  */
  OXGN_CLP_API auto Add(const std::shared_ptr<Option>& option) -> void;

  auto begin() -> OptionsCollectionType::iterator { return options.begin(); }

  auto end() -> OptionsCollectionType::iterator { return options.end(); }

  [[nodiscard]] auto cbegin() const noexcept
    -> OptionsCollectionType::const_iterator
  {
    return options.cbegin();
  }

  [[nodiscard]] auto cend() const noexcept
    -> OptionsCollectionType::const_iterator
  {
    return options.cend();
  }

  /** Outputs 'desc' to the specified stream, calling 'f' to output each
      option_description element. */
  OXGN_CLP_API auto Print(
    const CommandLineContext& context, unsigned width = 0) const -> void;

private:
  std::string label;
  OptionsCollectionType options;
};

} // namespace oxygen::clap
