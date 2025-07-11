//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <any>
#include <functional>
#include <sstream>
#include <string>

#include <Oxygen/Clap/Detail/ParseValue.h>
#include <Oxygen/Clap/ValueSemantics.h>

namespace oxygen::clap {

//! Defines the semantics (i.e. how the parser treats values found on the
//! command line) for a value of type `T`.
/*!
 Instances of this class cannot be created directly, copied or moved. Instead,
 an `OptionValueBuilder` is used to create them, and there is only one instance
 for each `Option`.

 ### Value storage

 Besides what the command line parser offers as a means to collect and access
 the option values, the interface in ValueDescriptor offers two ways to get the
 final value after it is determined:

 - via the `StoreTo` optional parameter passed to the constructor. If not null,
   this memory location will hold the value after parsing is complete;

 - via the notifier callback passed to Notifier(). If provided, this callback
   function will be called a value for the option is determined.

 \note the notifier callback may be called multiple times for the same option if
 that option is repeatable.

 @see OptionValueBuilder for a more intuitive way of specifying value semantics
 for an option.
*/
template <class T> class ValueDescriptor : public ValueSemantics {
public:
  ~ValueDescriptor() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ValueDescriptor)
  OXYGEN_MAKE_NON_MOVABLE(ValueDescriptor)

  [[nodiscard]] auto UserFriendlyName() const -> const std::string& override
  {
    return user_friendly_name_;
  }

  auto UserFriendlyName(std::string name) -> void
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
  auto StoreTo(T* store_to) -> void { store_to_ = store_to; }
  // TODO(abdes) test case for ValueDescriptor with value_store

  //! Specifies a default value, which will be used if the option is not
  //! present on the command line.
  /*!
   The default value is not to be confused with the implicit value. The latter
   is only used when the option is present but the value is missing. The former
   is used when the option is not present at all.

   The textual form of the value is used for debugging and troubleshooting only.

   @see ImplicitValue
  */
  auto DefaultValue(const T& value, const std::string& textual) -> void
  {
    default_value_ = std::any { value };
    default_value_as_text_ = textual;
  }

  /*!
   @copydoc DefaultValue(const T &, const std::string &)

   @note In this form, the textual value is automatically deduced through
   streaming, and therefore, the type 'T' should provide `operator <<` for
   `ostream`.
  */
  auto DefaultValue(const T& value) -> void
  {
    default_value_ = std::any { value };
    std::ostringstream string_converter;
    string_converter << value;
    default_value_as_text_ = string_converter.str();
  }

  //! Returns whether the option has a default value.
  [[nodiscard]] auto HasDefaultValue() const -> bool override
  {
    return default_value_.has_value();
  }

  //! Specifies an implicit value, which will be used if the option is given,
  //! but without an adjacent value.
  /*!
   @note Defining an implicit value implies that an explicit value is optional,
   while not defining an implicit value implies that an explicit value is
   required.

   The textual form of the value is used for debugging and troubleshooting only.

   @see IsRequired
   */
  auto ImplicitValue(const T& value, const std::string& textual) -> void
  {
    implicit_value_ = std::any(value);
    implicit_value_as_text_ = textual;
  }

  /*!
   @copydoc ImplicitValue(const T &, const std::string &)

   @note In this form, the textual value is automatically deduced through
   streaming, and therefore, the type 'T' should provide `operator <<` for
   `ostream`.
   */
  auto ImplicitValue(const T& value) -> void
  {
    implicit_value_ = std::any(value);
    std::ostringstream string_converter;
    string_converter << value;
    implicit_value_as_text_ = string_converter.str();
  }

  //! Specifies that the option can appear multiple times on the command line
  //! (i.e. it can accept multiple values).
  auto Repeatable() -> void { repeatable_ = true; }

  //! Returns whether the option can appear multiple times on the command line.
  [[nodiscard]] auto IsRepeatable() const -> bool override
  {
    return repeatable_;
  }

  /*!
   @copydoc ValueSemantics::IsRequired()

   @note In this implementation, if an implicit value is specified, an explicit
   value is optional; otherwise, it is required.

   @see ImplicitValue
   */
  [[nodiscard]] auto IsRequired() const -> bool override
  {
    return !implicit_value_.has_value();
  }

  // TODO(abdes) document currently available value type parsers
  auto Parse(std::any& value_store, const std::string& token) const
    -> bool override
  {
    // TODO(abdes) implement additional value type parsers
    T parsed;
    if (detail::ParseValue(token, parsed)) {
      value_store = parsed;
      return true;
    }
    return false;
  }

  /**
   * \brief If a default value was specified via a previous call to
   * `DefaultValue`, applies that value to the `value_store` and
   * `value_as_text`.
   *
   * \return *true* if a default value was applied.
   */

  //! Applies the default value to the given storage if one was set via
  //! `DefaultValue()`.
  /*!
   Called during the command line parsing. If a default value exists, assigns it
   to \p value_store and its textual representation to \p value_as_text.

   @return true if a default value was applied; false otherwise.
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

  //! Applies the implicit value to the given storage if one was set via
  //! `ImplicitValue()`.
  /*!
   Called during the command line parsing. If an implicit value exists, assigns
   it to \p value_store and its textual representation to \p value_as_text.

   @return true if an implicit value was applied; false otherwise.
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

  // TODO: deicide if and how we can get notified not just for the final value,
  // but for other values as well

  //! Specifies a 'Callable' (function, lambda, method, etc.) to be invoked with
  //! a const reference to the value, when the final value is determined.
  template <typename Callable>
    requires std::invocable<Callable, const T&>
  auto CallOnFinalValue(Callable&& callback) -> void
  {
    // Keep the callback as type-erased std::function
    notifier_ = std::function<void(const T&)>(std::forward<Callable>(callback));
  }

  // TODO(abdes) refactor callback interface
  //  - Allow to pass a callback function that gets called by the parser. The
  //  callback is always invoked (when an option is specified, when an option
  //  is not specified and when a default value is assigned to the option)
  //  ==> add Notify(Callback) to ValueDescriptor class
  //  - Allow for the callback to be setup to be called on every value instead
  //  of when the final value(s) is(are) determined at the ned of parsing
  //  ==> add NotifyOnEachValue(Callback) to ValueDescriptor class

  //! Called when final value of an option is determined.
  /*!
   This method two purposes:
   - If an address of a variable to store the value was specified when
     describing the value (via the ValueDescriptorBuilder), stores the value
     there.
   - If a  notification callback was provided via a previous call to Notifier(),
     call that function.
   */
  auto OnFinalValue(const std::any& value_store) const -> void
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

} // namespace oxygen::clap
