//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/TextWrap/TextWrap.h>

namespace oxygen::clap::detail {

inline auto MakeStyledWrapper(const unsigned int width, std::string initial,
  std::string then) -> wrap::TextWrapper
{
  return wrap::MakeWrapper()
    .Width(width)
    .IgnoreAnsiEscapeCodes()
    .CollapseWhiteSpace()
    .TrimLines()
    .IndentWith()
    .Initially(std::move(initial))
    .Then(std::move(then));
}

} // namespace oxygen::clap::detail
