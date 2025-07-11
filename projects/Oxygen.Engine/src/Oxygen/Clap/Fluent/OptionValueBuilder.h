//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stdexcept>

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
    if (!value_descriptor_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    value_descriptor_->StoreTo(store_to);
    return *this;
  }

  auto UserFriendlyName(std::string name) -> Self&
  {
    if (!value_descriptor_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    value_descriptor_->UserFriendlyName(name);
    return *this;
  }

  auto DefaultValue(const T& value) -> OptionValueBuilder&
  {
    if (!value_descriptor_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    value_descriptor_->DefaultValue(value);
    return *this;
  }

  auto DefaultValue(const T& value, const std::string& textual)
    -> OptionValueBuilder&
  {
    if (!value_descriptor_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    value_descriptor_->DefaultValue(value, textual);
    return *this;
  }

  auto ImplicitValue(const T& value) -> OptionValueBuilder&
  {
    if (!value_descriptor_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    value_descriptor_->ImplicitValue(value);
    return *this;
  }

  auto ImplicitValue(const T& value, const std::string& textual)
    -> OptionValueBuilder&
  {
    if (!value_descriptor_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    value_descriptor_->ImplicitValue(value, textual);
    return *this;
  }

  auto Repeatable() -> OptionValueBuilder&
  {
    if (!value_descriptor_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    value_descriptor_->Repeatable();
    return *this;
  }

private:
  std::shared_ptr<ValueDescriptor<T>> value_descriptor_;
};

} // namespace oxygen::clap
