//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Fluent/OptionBuilder.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>

auto asap::clap::OptionBuilder::Short(std::string short_name) -> Self&
{
  DCHECK_NOTNULL_F(option_, "builder used after Build() was called");
  option_->Short(std::move(short_name));
  return *this;
}

auto asap::clap::OptionBuilder::Long(std::string long_name) -> Self&
{
  DCHECK_NOTNULL_F(option_, "builder used after Build() was called");
  option_->Long(std::move(long_name));
  return *this;
}

auto asap::clap::OptionBuilder::About(std::string about) -> Self&
{
  DCHECK_NOTNULL_F(option_, "builder used after Build() was called");
  option_->About(std::move(about));
  return *this;
}

auto asap::clap::OptionBuilder::Required() -> Self&
{
  DCHECK_NOTNULL_F(option_, "builder used after Build() was called");
  option_->Required();
  return *this;
}

auto asap::clap::OptionBuilder::UserFriendlyName(std::string name) -> Self&
{
  DCHECK_NOTNULL_F(option_, "builder used after Build() was called");
  option_->UserFriendlyName(std::move(name));
  return *this;
}

template <>
auto asap::clap::OptionBuilder::WithValue<bool>() -> OptionValueBuilder<bool>
{
  OptionValueBuilder<bool> value_builder(std::move(option_));
  value_builder.ImplicitValue(true, "true");
  return value_builder;
}
