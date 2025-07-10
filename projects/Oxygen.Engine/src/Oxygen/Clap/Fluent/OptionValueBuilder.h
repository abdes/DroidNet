//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Fluent/OptionBuilder.h>
#include <Oxygen/Clap/Internal/ValueDescriptor.h>

namespace oxygen::clap {

template <class T> class OptionValueBuilder : public OptionBuilder {
  using Self = OptionValueBuilder;

public:
  explicit OptionValueBuilder(std::unique_ptr<Option> option)
    : OptionBuilder(std::move(option))
    , value_descriptor_(new ValueDescriptor<T>())
  {
    option_->value_semantic_ = value_descriptor_;
  }

  auto StoreTo(T* store_to) -> Self&
  {
    DCHECK_NOTNULL_F(
      value_descriptor_, "builder used after Build() was called");
    value_descriptor_->StoreTo(store_to);
    return *this;
  }

  auto UserFriendlyName(std::string name) -> Self&
  {
    DCHECK_NOTNULL_F(
      value_descriptor_, "builder used after Build() was called");
    value_descriptor_->UserFriendlyName(name);
    return *this;
  }

  auto DefaultValue(const T& value) -> OptionValueBuilder&
  {
    DCHECK_NOTNULL_F(
      value_descriptor_, "builder used after Build() was called");
    value_descriptor_->DefaultValue(value);
    return *this;
  }

  auto DefaultValue(const T& value, const std::string& textual)
    -> OptionValueBuilder&
  {
    DCHECK_NOTNULL_F(
      value_descriptor_, "builder used after Build() was called");
    value_descriptor_->DefaultValue(value, textual);
    return *this;
  }

  auto ImplicitValue(const T& value) -> OptionValueBuilder&
  {
    DCHECK_NOTNULL_F(
      value_descriptor_, "builder used after Build() was called");
    value_descriptor_->ImplicitValue(value);
    return *this;
  }

  auto ImplicitValue(const T& value, const std::string& textual)
    -> OptionValueBuilder&
  {
    DCHECK_NOTNULL_F(
      value_descriptor_, "builder used after Build() was called");
    value_descriptor_->ImplicitValue(value, textual);
    return *this;
  }

  auto Repeatable() -> OptionValueBuilder&
  {
    DCHECK_NOTNULL_F(
      value_descriptor_, "builder used after Build() was called");
    value_descriptor_->Repeatable();
    return *this;
  }

private:
  std::shared_ptr<ValueDescriptor<T>> value_descriptor_;
};

// template <>
// inline OptionValueBuilder<bool>::OptionValueBuilder()
//     : option_value_(new ValueDescriptor<bool>(store_to)) {
//   option_value_->DefaultValue(false, "false");
//   option_value_->ImplicitValue(true, "true");
// }

// /**
//  * \brief Make a builder to start describing a new option value.
//  *
//  * This factory method optionally takes as an argument a location
//  * `store_to` which (when not null) will hold the final value(s).
//  *
//  * \see Notifier, Notify
//  */
// template <typename T>
// auto CreateValueDescriptor(T *store_to = nullptr) ->
//     typename ValueDescriptor<T>::Builder {
//   return ValueDescriptor<T>::Builder(store_to);
// }

} // namespace oxygen::clap
