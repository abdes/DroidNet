//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>

namespace oxygen::clap {

template <typename Builder,
  std::enable_if_t<std::is_same_v<Builder, OptionBuilder>, std::nullptr_t>>
class Positional : Builder {
public:
  explicit Positional(std::string key)
    : Builder(std::move(key))
  {
  }

  auto About(std::string about) -> Positional<Builder>&
  {
    Builder::About(std::forward<std::string>(about));
    return *this;
  }

  auto UserFriendlyName(std::string name) -> Positional<Builder>&
  {
    Builder::UserFriendlyName(std::move(name));
    return *this;
  }

  auto Required() -> Positional<Builder>&
  {
    Builder::Required();
    return *this;
  }

  template <typename T>
  auto WithValue(typename ValueDescriptor<T>::Builder& option_value_builder)
    -> Positional<Builder>&
  {
    Builder::template WithValue<T>(option_value_builder);
    return *this;
  }

  template <typename T>
  auto WithValue(typename ValueDescriptor<T>::Builder&& option_value_builder)
    -> Positional<Builder>&
  {
    Builder::template WithValue<T>(std::move(option_value_builder));
    return *this;
  }

  using Builder::Build;
  using Builder::WithValue;
};

} // namespace oxygen::clap
