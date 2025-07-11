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
  {
    // Ensure value_semantic_ is a non-const shared_ptr<ValueSemantics>
    option_->value_semantic_ = std::static_pointer_cast<ValueSemantics>(
      std::shared_ptr<ValueDescriptor<T>>(new ValueDescriptor<T>()));
  }

  auto StoreTo(T* store_to) -> Self&
  {
    TypedValueDesc()->StoreTo(store_to);
    return *this;
  }

  auto UserFriendlyName(std::string name) -> Self&
  {
    TypedValueDesc()->UserFriendlyName(name);
    return *this;
  }

  auto DefaultValue(const T& value) -> OptionValueBuilder&
  {
    TypedValueDesc()->DefaultValue(value);
    return *this;
  }

  auto DefaultValue(const T& value, const std::string& textual)
    -> OptionValueBuilder&
  {
    TypedValueDesc()->DefaultValue(value, textual);
    return *this;
  }

  auto ImplicitValue(const T& value) -> OptionValueBuilder&
  {
    TypedValueDesc()->ImplicitValue(value);
    return *this;
  }

  auto ImplicitValue(const T& value, const std::string& textual)
    -> OptionValueBuilder&
  {
    TypedValueDesc()->ImplicitValue(value, textual);
    return *this;
  }

  auto Repeatable() -> OptionValueBuilder&
  {
    TypedValueDesc()->Repeatable();
    return *this;
  }

private:
  auto TypedValueDesc() const -> std::shared_ptr<ValueDescriptor<T>>
  {
    if (!option_ || !option_->value_semantic_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    // Cast from non-const base pointer
    return std::static_pointer_cast<ValueDescriptor<T>>(
      option_->value_semantic_);
  }
};

} // namespace oxygen::clap
