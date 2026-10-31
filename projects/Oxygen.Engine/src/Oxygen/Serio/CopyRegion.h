//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Result.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AsyncDevice.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! The chunk `CopyRegion` reads and writes when the caller gives no scratch.
inline constexpr auto kCopyChunk = ByteCount { uint64_t { 1 } << 20U };

//! Copies `from` into `to` at `at`, reading the next chunk while it writes
//! the current one.
/*!
 `scratch` holds two chunks of half its size; the caller owns it, for
 example from `IoDriver::AllocateBuffer`, and keeps it valid until the copy
 completes. On a file opened through an `IoDriver`, the read of chunk N + 1
 and the write of chunk N are in flight together.

 @return Success once every byte is written; `kEndOfData` when `from` ends
         before its range does; `invalid_argument` for scratch smaller than
         two bytes; device errors unchanged. A failed copy leaves `to`
         partially written.
*/
template <AsyncReadableDevice S, AsyncWritableDevice D>
[[nodiscard]] auto CopyRegion(const Region<S> from, D& to, const ByteOffset at,
  const std::span<std::byte> scratch) -> co::Co<Result<void>>
{
  const auto chunk = scratch.size() / 2;
  if (chunk == 0) {
    co_return ::oxygen::Err(std::make_error_code(std::errc::invalid_argument));
  }
  const auto total = from.Range().count.get();
  const auto size_at = [total, chunk](const uint64_t offset) -> size_t {
    return static_cast<size_t>((std::min)(uint64_t { chunk }, total - offset));
  };

  auto current = scratch.first(chunk);
  auto next = scratch.subspan(chunk, chunk);
  auto offset = uint64_t { 0 };
  auto size = size_at(offset);
  if (size == 0) {
    co_return Result<void> {};
  }
  if (const auto read
    = co_await AsyncReadExactlyAt(from, ByteOffset { 0 }, current.first(size));
    !read) {
    co_return ::oxygen::Err(read.error());
  }
  while (true) {
    const auto write_at = at + ByteCount { offset };
    const auto next_offset = offset + size;
    if (next_offset == total) {
      co_return co_await to.AsyncWriteAt(write_at, current.first(size));
    }
    const auto next_size = size_at(next_offset);
    const auto [written, read]
      = co_await co::AllOf(to.AsyncWriteAt(write_at, current.first(size)),
        AsyncReadExactlyAt(
          from, ByteOffset { next_offset }, next.first(next_size)));
    if (!written) {
      co_return ::oxygen::Err(written.error());
    }
    if (!read) {
      co_return ::oxygen::Err(read.error());
    }
    offset = next_offset;
    size = next_size;
    std::swap(current, next);
  }
}

//! `CopyRegion` with scratch of two `kCopyChunk` chunks, or less for a
//! smaller region, allocated for the copy.
template <AsyncReadableDevice S, AsyncWritableDevice D>
[[nodiscard]] auto CopyRegion(const Region<S> from, D& to, const ByteOffset at)
  -> co::Co<Result<void>>
{
  const auto chunk = (std::min)(kCopyChunk.get(), from.Range().count.get());
  if (chunk == 0) {
    co_return Result<void> {};
  }
  auto scratch = ByteBuffer::Allocate(ByteCount { 2 * chunk });
  if (!scratch) {
    co_return ::oxygen::Err(scratch.error());
  }
  co_return co_await CopyRegion(from, to, at, scratch->Bytes());
}

} // namespace oxygen::serio
