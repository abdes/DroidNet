//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! Fills `out` from `device` at `at`, awaiting each read, or fails with
//! `kEndOfData` when the device ends first. Device errors propagate
//! unchanged; `out` stays valid until the coroutine completes.
template <AsyncReadableDevice D>
[[nodiscard]] auto AsyncReadExactlyAt(const D& device, ByteOffset at,
  std::span<std::byte> out) -> co::Co<Result<void>>
{
  while (!out.empty()) {
    const auto read = co_await device.AsyncReadAt(at, out);
    if (!read) {
      co_return ::oxygen::Err(read.error());
    }
    if (read->get() == 0) {
      co_return ::oxygen::Err(make_error_code(Error::kEndOfData));
    }
    out = out.subspan(static_cast<size_t>(read->get()));
    at += *read;
  }
  co_return Result<void> {};
}

//! `ReadAll`, awaiting each read; a device larger than `limit` is refused
//! before anything is allocated.
/*!
 @return The buffer, `Size()` bytes long; `kTooLarge` when the size passes
         `limit`; `kEndOfData` when the device ends before its size, the
         allocation's or the device's errors otherwise.
*/
template <AsyncReadableDevice D>
[[nodiscard]] auto AsyncReadAll(const D& device, const ByteCount limit)
  -> co::Co<Result<ByteBuffer>>
{
  const auto size = device.Size();
  if (!size) {
    co_return ::oxygen::Err(size.error());
  }
  if (*size > limit) {
    co_return ::oxygen::Err(make_error_code(Error::kTooLarge));
  }
  auto buffer = ByteBuffer::Allocate(*size);
  if (!buffer) {
    co_return buffer;
  }
  if (const auto read
    = co_await AsyncReadExactlyAt(device, ByteOffset { 0 }, buffer->Bytes());
    !read) {
    co_return ::oxygen::Err(read.error());
  }
  co_return buffer;
}

} // namespace oxygen::serio
