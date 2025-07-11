//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Clap/Fluent/OptionBuilder.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>

// ReSharper disable CppClangTidyMiscUseInternalLinkage

template <>
auto oxygen::clap::OptionBuilder::WithValue<bool>() -> OptionValueBuilder<bool>
{
  OptionValueBuilder<bool> value_builder(std::move(option_));
  value_builder.ImplicitValue(true, "true");
  return value_builder;
}
