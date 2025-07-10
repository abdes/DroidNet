//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <any>
#include <string>

/// Namespace for command line parsing related APIs.
namespace asap::clap {

/*!
 * \brief Represents a value for a command line option.
 *
 * This class encapsulates a command line option value of any type, information
 * about its origin and allows type-safe access to it.
 */
class OptionValue {
public:
  /*!
   * \brief Creates a new OptionValue.
   *
   * \param value the value that will be stored.
   * \param original_token the token from which this option value was parsed.
   * \param defaulted when \b true, indicates that the stored value comes from a
   * default value rather than from an explicit value on the command line.
   */
  OptionValue(std::any value, std::string original_token, bool defaulted)
    : value_ { std::move(value) }
    , original_token_(std::move(original_token))
    , defaulted_ { defaulted }
  {
  }

  OptionValue(const OptionValue&) = default;
  OptionValue(OptionValue&&) = default;

  auto operator=(const OptionValue&) -> OptionValue& = delete;
  auto operator=(OptionValue&&) -> OptionValue& = delete;

  ~OptionValue() = default;

  /*!
   * \copydoc GetAs() -> T &
   */
  template <typename T> [[nodiscard]] auto GetAs() const -> const T&
  {
    return std::any_cast<const T&>(value_);
  }

  /*!
   * \brief If the stored value has type T, returns that value; otherwise throws
   * std::bad_any_cast.
   */
  template <typename T> [[nodiscard]] auto GetAs() -> T&
  {
    return std::any_cast<T&>(value_);
  }

  /*!
   * \brief Checks whether the stored value came from the default value or was
   * explicitly specified on the command line.
   */
  [[nodiscard]] auto IsDefaulted() const -> bool { return defaulted_; }

  /*!
   * \brief Returns the original token from which this option value was parsed.
   */
  [[nodiscard]] auto OriginalToken() const -> const std::string&
  {
    return original_token_;
  }

  /*!
   * \copydoc Value() -> std::any &
   */
  [[nodiscard]] auto Value() const -> const std::any& { return value_; }

  /*!
   * \brief Returns the stored value.
   */
  [[nodiscard]] auto Value() -> std::any& { return value_; }

private:
  std::any value_;
  std::string original_token_;
  bool defaulted_ { false };
};

} // namespace asap::clap
