//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Windows.h> // IWYU pragma: keep

#include <bit>
#include <cstddef>
#include <limits>

#include <Oxygen/Serio/NativeFile.h>

namespace oxygen::serio::windows {

//! The largest transfer one `ReadFile`/`WriteFile` call is given.
/*!
 A `DWORD` counts at most 4 GiB - 1 bytes, which is no sector multiple, and
 an unbuffered file takes only sector multiples. The largest power of two a
 `DWORD` holds is a multiple of every sector size, so a long aligned transfer
 splits into aligned pieces.
*/
inline constexpr auto kMaxTransfer
  = static_cast<std::size_t>(std::bit_floor(std::numeric_limits<DWORD>::max()));
static_assert(kMaxTransfer % kIoAlignment == 0);

//! The Win32 handle a `NativeHandle` holds.
[[nodiscard]] inline auto ToHandle(const NativeHandle handle) noexcept -> HANDLE
{
  return std::bit_cast<HANDLE>(handle);
}

//! The `NativeHandle` that holds a Win32 handle.
[[nodiscard]] inline auto ToNativeHandle(HANDLE handle) noexcept -> NativeHandle
{
  return std::bit_cast<NativeHandle>(handle);
}

} // namespace oxygen::serio::windows
