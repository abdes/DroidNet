//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <any>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap {

/*!
 * \brief Describes how a command line option's value is to be parsed and
 * converted into C++ types.
 *
 * For options that take values it must be specified whether the option value is
 * required or not, can be repeated or not..., has a default value or an
 * implicit value and what kind of value the option expects.
 *
 * ### Multi-value Options
 *
 * Multiple values can be provided to an option via a proprietary format, such
 * as comma separated values or other, to be parsed by a custom value parser. To
 * make it possible to do so without the need for a custom parser, the API also
 * supports repeating an option multiple times on the command line. Each
 * occurrence provides one more value.
 *
 * ### Flag Options
 *
 * Some options, such as boolean flags, do not take values. Their mere presence
 * on the command line corresponds to a specific value (such as *true*) and
 * their absence usually corresponds to the opposite value (such as *false*).
 * The value to be used when the option is present is called *Implicit Value*,
 * not to be confused with a potential default value for the option, which is
 * used when the option is not present on the command line.
 *
 * ### Design notes
 *
 * This is the interface used by the command line parser to interact with
 * options while parsing their values and validating them. The interface is
 * quite generic by design so that the parser does not really care about the
 * specific option value's type. Instead, it only manipulates values of type
 * `std::any`. The concrete implementation of this interface will deal with
 * specific value types.
 */
class OXGN_CLP_API ValueSemantics {
public:
  ValueSemantics() = default;

  virtual ~ValueSemantics() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(ValueSemantics)
  OXYGEN_MAKE_NON_MOVABLE(ValueSemantics)

  [[nodiscard]] virtual auto UserFriendlyName() const -> const std::string& = 0;

  [[nodiscard]] virtual auto IsFlag() const -> bool = 0;

  /**
   * \brief Indicates if this option can occur multiple times on the command
   * line, allowing for it to take multiple values.
   */
  [[nodiscard]] virtual auto IsRepeatable() const -> bool = 0;
  // TODO(abdes) Ensure there are test cases for repeatable value

  /**
   * \brief Indicates if this option requires a value to be specified.
   *
   * When this is \b true, the command line parser requires that:
   * - each occurrence of the option is accompanied by  a value,
   * - or an implicit value is specified if the option is encountered with no
   *   value,
   * - or a default value is specified if the option is not on the command line.
   */
  [[nodiscard]] virtual auto IsRequired() const -> bool = 0;
  // TODO(abdes) Ensure there are test cases for required values

  [[nodiscard]] virtual auto HasDefaultValue() const -> bool = 0;

  /**
   * \brief Assign the default value to 'value_store'.
   *
   * This abstract method needs to be implemented by concrete value descriptors
   * to eventually assign the default value to the value store. It is
   * particularly useful when an option with a required value was not specified
   * (the option) on the command line.
   *
   * \return *true* if the default value is assigned, and *false* if no default
   * value exists.
   */
  virtual auto ApplyDefault(
    std::any& value_store, std::string& value_as_text) const -> bool
    = 0;

  /**
   * \brief Assign the implicit value to 'value_store'.
   *
   * This abstract method needs to be implemented by concrete value descriptors
   * to eventually assign the implicit value to the value store. It is
   * particularly useful when an option with a required value was encountered on
   * the command line but without a value.
   *
   * \return *true* if the implicit value is assigned, and *false* if no
   * implicit value exists.
   */
  virtual auto ApplyImplicit(
    std::any& value_store, std::string& value_as_text) const -> bool
    = 0;

  /**
   * \brief Parse a token to extract from it a value for an option.
   *
   * Stores the result in 'value_store', using whatever representation is
   * desired.
   *
   * \return *true* if the parsing resulted in a suitable value extracted from
   * the token for the option, and *false* if the token could not be interpreted
   * as a suitable value.
   *
   * \note Failure to extract a value from the token is not necessarily an
   * error. It simply indicates that the token is not a value for the option.
   * The parser will continue interpreting that token as something else as
   * specified by the command line (e.g. a positional argument).
   */
  virtual auto Parse(std::any& value_store, const std::string& token) const
    -> bool
    = 0;

  //! Notify listeners for each parsed value.
  /*! Called immediately after a value is parsed and stored. Defaults to a
      no-op. */
  virtual auto NotifyParsed(const std::any& /*value_store*/) const -> void { }

  //! Finalize the value by applying storage / notification side-effects.
  /*! Called after the full command line has been parsed so that concrete
      implementations can propagate the final (possibly default or implicit)
      value to user-provided storage (StoreTo) or invoke callbacks. The
      default implementation does nothing. */
  virtual auto Finalize(const std::any& /*value_store*/) const -> void { }
};

} // namespace oxygen::clap
