//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// Helper for MSVC
#if defined(_MSC_VER)
#  include <intrin.h>
#  pragma intrinsic(_ReturnAddress)
namespace oxygen::detail {
[[nodiscard]] inline void* GetReturnAddress() noexcept
{
  return _ReturnAddress();
}
} // namespace oxygen::detail
#elif defined(__GNUC__) || defined(__clang__)
namespace oxygen::detail {
[[nodiscard]] inline void* GetReturnAddress() noexcept
{
  return __builtin_return_address(0);
}
} // namespace oxygen::detail
#else
namespace oxygen::detail {
[[nodiscard]] inline void* GetReturnAddress() noexcept { return nullptr; }
} // namespace oxygen::detail
#endif

namespace oxygen {
// User-facing constexpr template function
template <typename T = void>
[[nodiscard]] constexpr void* ReturnAddress() noexcept
{
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
  return detail::GetReturnAddress();
#else
  return nullptr;
#endif
}

} // namespace oxygen
