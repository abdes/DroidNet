//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <utility>

#include <Oxygen/Clap/Option.h>
#include <Oxygen/Clap/api_export.h>

namespace asap::clap {

template <typename T> class OptionValueBuilder;

class OptionBuilder {
  using Self = OptionBuilder;

public:
  explicit OptionBuilder(std::string key)
    : option_(new Option(std::move(key)))
  {
  }

  OXGN_CLP_API auto Short(std::string short_name) -> Self&;

  OXGN_CLP_API auto Long(std::string long_name) -> Self&;

  OXGN_CLP_API auto About(std::string about) -> Self&;
  OXGN_CLP_API auto Required() -> Self&;

  OXGN_CLP_API auto UserFriendlyName(std::string name) -> Self&;

  // template <typename T>
  // auto WithValue(typename ValueDescriptor<T>::Builder &option_value_builder)
  //     -> OptionBuilder & {
  //   ASAP_ASSERT(option_ && "builder used after Build() was called");
  //   option_->value_semantic_ = std::move(option_value_builder.Build());
  //   return *this;
  // }

  // template <typename T>
  // auto WithValue(typename ValueDescriptor<T>::Builder &&option_value_builder)
  //     -> OptionBuilder & {
  //   ASAP_ASSERT(option_ && "builder used after Build() was called");
  //   option_->value_semantic_ = std::move(option_value_builder.Build());
  //   return *this;
  // }

  auto Build() -> std::shared_ptr<Option> { return std::move(option_); }

  // Builder facets

  template <typename T> auto WithValue() -> OptionValueBuilder<T>
  {
    return OptionValueBuilder<T>(std::move(option_));
  }

protected:
  // We use a `unique_ptr` here instead of a simple contained object in order to
  // have an option initialized at construction of the builder, and then once
  // moved out, the builder becomes explicitly invalid.
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  std::unique_ptr<Option> option_;

  explicit OptionBuilder(std::unique_ptr<Option> option)
    : option_ { std::move(option) }
  {
  }
};

template <>
OXGN_CLP_API auto asap::clap::OptionBuilder::WithValue<bool>()
  -> OptionValueBuilder<bool>;

} // namespace asap::clap
