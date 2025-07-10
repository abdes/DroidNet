//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <any>
#include <functional>
#include <sstream>
#include <string>

#include <Oxygen/Clap/Detail/ParseValue.h>
#include <Oxygen/Clap/ValueSemantics.h>

namespace asap::clap {

/*!
 * \brief The concrete implementation of `ValueSemantics` interface for a value
 * of type `T`.
 *
 * Instances of this class cannot be created directly. Instead, use the
 * ValueDescriptor::Builder class to create them
 *
 * ### Value storage
 *
 * Besides what the command line parser offers as a means to collect and access
 * the option values, the interface in ValueDescriptor offers two ways to get
 * the final value after it is determined:
 *
 * - via the `store_to` optional parameter passed to the constructor. If not
 *   null, this memory location will hold the value after parsing is complete;
 *
 * - via the notifier callback passed to Notifier(). If provided, this callback
 *   function will be called a value for the option is determined.
 *
 * \note the notifier callback may be called multiple times for the same option
 * if that option is repeatable.
 *
 * \see ValueDescriptorBuilder for a more intuitive way of specifying value
 * semantics for an option.
 */
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
template <class T> class ValueDescriptor : public ValueSemantics {
public:
  ValueDescriptor(const ValueDescriptor&) = delete;
  auto operator=(const ValueDescriptor&) -> ValueDescriptor& = delete;
  ValueDescriptor(ValueDescriptor&&) = delete;
  auto operator=(ValueDescriptor&&) -> ValueDescriptor& = delete;

  ~ValueDescriptor() override = default;

  [[nodiscard]] auto UserFriendlyName() const -> const std::string& override
  {
    return user_friendly_name_;
  }

  void UserFriendlyName(std::string name)
  {
    user_friendly_name_ = std::move(name);
  }

  [[nodiscard]] auto IsFlag() const -> bool override
  {
    return std::is_same<T, bool>();
  }

  /**
   * \brief Use the provided address to store the value when it's known.
   *
   * This is one of the multiple ways to collect parsed values.
   *
   * \see Notifier
   */
  void StoreTo(T* store_to) { store_to_ = store_to; }
  // TODO(Abdessattar) test case for ValueDescriptor with value_store

  /**
   * \copydoc DefaultValue(const T &, const std::string &)
   *
   * \note In this form, the textual value is automatically deduced through
   * streaming, and therefore, the type 'T' should provide operator<< for
   * ostream.
   *
   * \see ImplicitValue
   */
  void DefaultValue(const T& value)
  {
    default_value_ = std::any { value };
    std::ostringstream string_converter;
    string_converter << value;
    default_value_as_text_ = string_converter.str();
  }

  /**
   * \brief Specifies a default value, which will be used if the option is not
   * present on the command line.
   *
   * The default value is not to be confused with the implicit value. The latter
   * is only used when the option is present but the value is missing. The
   * former is used when the option is not present at all.
   *
   * The textual form of the value is used for troubleshooting only.
   *
   * \see ImplicitValue
   */
  void DefaultValue(const T& value, const std::string& textual)
  {
    default_value_ = std::any { value };
    default_value_as_text_ = textual;
  }

  /**
   * \copydoc DefaultValue(const T &, const std::string &)
   *
   * \note In this form, the textual value is automatically deduced through
   * streaming, and therefore, the type 'T' should provide operator<< for
   * ostream.
   *
   * \see Required
   */
  void ImplicitValue(const T& value)
  {
    implicit_value_ = std::any(value);
    std::ostringstream string_converter;
    string_converter << value;
    implicit_value_as_text_ = string_converter.str();
  }

  /**
   * \brief Specifies an implicit value, which will be used if the option is
   * given, but without an adjacent value.
   *
   * Defining an implicit value implies that an explicit value is optional,
   * while not defining an implicit value implies that an explicit value is
   * required.
   *
   * The textual form of the value is used for troubleshooting only.
   *
   * \see Required
   */
  void ImplicitValue(const T& value, const std::string& textual)
  {
    implicit_value_ = std::any(value);
    implicit_value_as_text_ = textual;
  }

  /**
   * \brief Specifies that the option can appear multiple times on the command
   * line (i.e. it can accept multiple values).
   */
  void Repeatable() { repeatable_ = true; }

  /**
   * \brief Specifies a function to be called when the final value
   * is determined.
   *
   * \see Notify
   */
  void Notifier(std::function<void(const T&)> callback)
  {
    notifier_ = callback;
  }

  [[nodiscard]] auto IsRepeatable() const -> bool override
  {
    return repeatable_;
  }

  /**
   * \copydoc ValueSemantics::IsRequired()
   *
   * \note Defining an implicit value implies that an explicit value is
   * optional, while not defining one implies that the explicit value is
   * required.
   *
   * \see ImplicitValue
   */
  [[nodiscard]] auto IsRequired() const -> bool override
  {
    return !implicit_value_.has_value();
  }

  [[nodiscard]] auto HasDefaultValue() const -> bool override
  {
    return default_value_.has_value();
  }

  // TODO(Abdessattar) document currently available value type parsers
  auto Parse(std::any& value_store, const std::string& token) const
    -> bool override
  {
    // TODO(Abdessattar) implement additional value type parsers
    T parsed;
    if (detail::ParseValue(token, parsed)) {
      value_store = parsed;
      return true;
    }
    return false;
  }

  /**
   * \brief If a default value was specified via a previous call to
   * DefaultValue(), applies that value to the `value_store` and
   * `value_as_text`.
   *
   * \return *true* if a default value was applied.
   */
  auto ApplyDefault(std::any& value_store, std::string& value_as_text) const
    -> bool override
  {
    if (!default_value_.has_value()) {
      return false;
    }
    value_store = default_value_;
    value_as_text = default_value_as_text_;
    return true;
  }

  /**
   * \brief If an implicit value was specified via a previous call to
   * ImplicitValue(), applies that value to the `value_store` and
   * `value_as_text`.
   *
   * \return *true* if a default value was applied.
   */
  auto ApplyImplicit(std::any& value_store, std::string& value_as_text) const
    -> bool override
  {
    if (!implicit_value_.has_value()) {
      return false;
    }
    value_store = implicit_value_;
    value_as_text = implicit_value_as_text_;
    return true;
  }

  /**
   * \copybrief ValueSemantics::Notify
   *
   * This method two purposes:
   * - If an address of a variable to store the value was specified when
   *   describing the value (via the ValueDescriptorBuilder), stores the value
   *   there.
   * - If a  notification callback was provided via a previous call to
   *   Notifier(), call that function.
   *
   * \see Create \see Notifier
   */
  void Notify(const std::any& value_store) const override
  {
    const T* value = std::any_cast<T>(&value_store);
    if (store_to_) {
      *store_to_ = *value;
    }
    if (notifier_) {
      notifier_(*value);
    }
  }

  template <typename> friend class OptionValueBuilder;

private:
  explicit ValueDescriptor() = default;

  std::string user_friendly_name_ { "value" };

  T* store_to_ = nullptr;

  std::any default_value_;
  std::string default_value_as_text_;
  std::any implicit_value_;
  std::string implicit_value_as_text_;
  bool repeatable_ { false };
  std::function<void(const T&)> notifier_;
};

} // namespace asap::clap
