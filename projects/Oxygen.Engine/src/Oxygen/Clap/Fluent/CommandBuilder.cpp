//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>

auto oxygen::clap::CommandBuilder::About(std::string about) -> Self&
{
  DCHECK_NOTNULL_F(command_, "builder used after Build() was called");
  command_->About(std::move(about));
  return *this;
}

auto oxygen::clap::CommandBuilder::WithOption(std::shared_ptr<Option> option)
  -> Self&
{
  DCHECK_NOTNULL_F(command_, "builder used after Build() was called");
  command_->WithOption(std::move(option));
  return *this;
}

auto oxygen::clap::CommandBuilder::WithOptions(
  std::shared_ptr<Options> options, const bool hidden) -> Self&
{
  DCHECK_NOTNULL_F(command_, "builder used after Build() was called");
  command_->WithOptions(std::move(options), hidden);
  return *this;
}
