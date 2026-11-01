//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstddef>
#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! Random-access bytes that name their offsets: a file, a mapping, memory.
/*!
 A device has no cursor, so one device serves concurrent callers without a
 lock. Every device honours the same contract, which the conformance suite
 checks:

 - `const` operations are safe from any number of threads at once;
 - `ReadAt` across the end returns the bytes that exist, and a read at or past
   the end returns zero bytes;
 - a zero-length operation succeeds and does nothing;
 - errors compare equal to the same `serio::Error` conditions on every device.

 @see WritableDevice, ContiguousDevice, ReadExactlyAt
*/
template <class D>
concept ReadableDevice = std::movable<D>
  && requires(const D& d, ByteOffset at, std::span<std::byte> out) {
       { d.Size() } noexcept -> std::same_as<Result<ByteCount>>;
       { d.ReadAt(at, out) } noexcept -> std::same_as<Result<ByteCount>>;
     };

//! A device that also writes, preallocates and flushes.
/*!
 `WriteAt` writes all of its bytes or fails. A write past the end extends the
 device; bytes between the old end and the write read as zero. `Reserve`
 preallocates storage without changing `Size()`.
*/
template <class D>
concept WritableDevice = ReadableDevice<D>
  && requires(D& d, ByteOffset at, std::span<const std::byte> in,
    ByteCount size, Durability durability) {
       { d.WriteAt(at, in) } noexcept -> std::same_as<Result<void>>;
       { d.Reserve(size) } noexcept -> std::same_as<Result<void>>;
       { d.Flush(durability) } noexcept -> std::same_as<Result<void>>;
     };

//! The whole device is one span of memory: parsers read views of it.
template <class D>
concept ContiguousDevice = ReadableDevice<D> && requires(const D& d) {
  { d.Bytes() } noexcept -> std::same_as<std::span<const std::byte>>;
};

//! A device whose reads can also be awaited in a coroutine.
/*!
 `AsyncReadAt` has `ReadAt`'s contract and returns a `ReadOperation`: a memory
 device's is ready at once, a file opened through an `IoDriver` suspends the
 awaiting coroutine until the OS completes it. One operation type for every
 device lets an erased device await without a coroutine frame.
*/
template <class D>
concept AsyncReadableDevice = ReadableDevice<D>
  && requires(const D& d, ByteOffset at, std::span<std::byte> out) {
       { d.AsyncReadAt(at, out) } noexcept -> std::same_as<ReadOperation>;
     };

//! A writable device whose reads and writes can be awaited; `AsyncWriteAt`
//! has `WriteAt`'s contract.
template <class D>
concept AsyncWritableDevice = AsyncReadableDevice<D> && WritableDevice<D>
  && requires(D& d, ByteOffset at, std::span<const std::byte> in) {
       { d.AsyncWriteAt(at, in) } noexcept -> std::same_as<WriteOperation>;
     };

//! Fills `out` from `device` at `at`, or fails with `kEndOfData` when the
//! device ends first. Device errors propagate unchanged.
template <ReadableDevice D>
[[nodiscard]] auto ReadExactlyAt(const D& device, ByteOffset at,
  std::span<std::byte> out) noexcept -> Result<void>
{
  while (!out.empty()) {
    const auto read = device.ReadAt(at, out);
    if (!read) {
      return ::oxygen::Err(read.error());
    }
    if (read->get() == 0) {
      return ::oxygen::Err(make_error_code(Error::kEndOfData));
    }
    const auto count = static_cast<size_t>(read->get());
    out = out.subspan(count);
    at += *read;
  }
  return {};
}

//! Reads all of `device` (a region reads its range) into a new buffer of at
//! most `limit` bytes.
/*!
 The size a device reports is whatever its file holds, so the caller names
 the most it accepts: a recorded size, a region's count, or its own policy.
 A larger device is refused before anything is allocated.

 @return The buffer, `Size()` bytes long; `kTooLarge` when the size passes
         `limit`; `kEndOfData` when the device ends before its size, the
         allocation's or the device's errors otherwise.
*/
template <ReadableDevice D>
[[nodiscard]] auto ReadAll(const D& device, const ByteCount limit) noexcept
  -> Result<ByteBuffer>
{
  const auto size = device.Size();
  if (!size) {
    return ::oxygen::Err(size.error());
  }
  if (*size > limit) {
    return ::oxygen::Err(make_error_code(Error::kTooLarge));
  }
  auto buffer = ByteBuffer::Allocate(*size);
  if (!buffer) {
    return buffer;
  }
  if (const auto read
    = ReadExactlyAt(device, ByteOffset { 0 }, buffer->Bytes());
    !read) {
    return ::oxygen::Err(read.error());
  }
  return buffer;
}

} // namespace oxygen::serio
