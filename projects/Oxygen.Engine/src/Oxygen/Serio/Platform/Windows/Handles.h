//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Windows.h> // IWYU pragma: keep

#include <bit>

#include <Oxygen/Serio/NativeFile.h>

namespace oxygen::serio::windows {

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
