//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <Oxygen/Clap/Option.h>
#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap {

class OptionBuilder {
  using Self = OptionBuilder;

public:
  explicit OptionBuilder(std::string key)
    : option_(new Option(std::move(key)))
  {
  }

  OXGN_CLP_API auto Short(std::string short_name) -> Self&
  {
    if (!option_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    option_->Short(std::move(short_name));
    return *this;
  }

  OXGN_CLP_API auto Long(std::string long_name) -> Self&
  {
    if (!option_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    option_->Long(std::move(long_name));
    return *this;
  }

  OXGN_CLP_API auto About(std::string about) -> Self&
  {
    if (!option_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    option_->About(std::move(about));
    return *this;
  }

  OXGN_CLP_API auto Required() -> Self&
  {
    if (!option_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    option_->Required();
    return *this;
  }

  OXGN_CLP_API auto UserFriendlyName(std::string name) -> Self&
  {
    if (!option_) {
      throw std::logic_error("OptionValueBuilder: method called after Build()");
    }
    option_->UserFriendlyName(std::move(name));
    return *this;
  }

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

template <typename T> class OptionValueBuilder;

//! Specialization of the OptionBuilder for boolean values, which will
//! automatically define an implicit value of `true`.
template <>
OXGN_CLP_NDAPI auto OptionBuilder::WithValue<bool>()
  -> OptionValueBuilder<bool>;

} // namespace oxygen::clap
