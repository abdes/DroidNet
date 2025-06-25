//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen {

[[noreturn]] inline void Unreachable()
{
#if defined(__cpp_lib_unreachable)
  std::unreachable();
#elif defined(__GNUC__) || defined(__clang__)
  __builtin_unreachable();
#elif defined(_MSC_VER)
  __assume(false);
#endif // would still invoke UB because of [[noreturn]]
}

} // namespace oxygen
