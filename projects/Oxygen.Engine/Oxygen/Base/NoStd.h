//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace nostd {

namespace adl_helper {

  template <class T>
  auto as_string(T&& value)
  {
    using std::to_string;
    return to_string(std::forward<T>(value));
  }

} // namespace adl_helper

template <class T>
auto to_string(T&& value)
{
  return adl_helper::as_string(std::forward<T>(value));
}

} // namespace nostd
