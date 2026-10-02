//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>

namespace oxygen::base {

//! Return a span element, throwing std::out_of_range for an invalid index.
template <typename T, std::size_t Extent>
[[nodiscard]] constexpr auto CheckedAt(
  std::span<T, Extent> values, const std::size_t index) -> T&
{
  if (index >= values.size()) {
    throw std::out_of_range("Span index out of range");
  }
  return *(values.data() + index);
}

} // namespace oxygen::base
