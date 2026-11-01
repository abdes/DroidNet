//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio::detail {

//! `NativeFile::ReadAt` of an open handle: platform code, which the inline
//! backend shares with the file.
[[nodiscard]] auto ReadAtHandle(NativeHandle file, ByteOffset at,
  std::span<std::byte> out) noexcept -> Result<ByteCount>;

//! `NativeFile::WriteAt` of an open handle.
[[nodiscard]] auto WriteAtHandle(NativeHandle file, ByteOffset at,
  std::span<const std::byte> in) noexcept -> Result<void>;

} // namespace oxygen::serio::detail
