//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <system_error>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/Result.h>

namespace oxygen::serio {

//! A position in a device or a stream, in bytes from its start.
using ByteOffset
  = NamedType<uint64_t, struct ByteOffsetTag, Comparable, Hashable, Printable>;

//! A number of bytes.
using ByteCount = NamedType<uint64_t, struct ByteCountTag, Comparable, Hashable,
  Printable, Addable, Subtractable>;

//! The offset `count` bytes after `offset`. The caller keeps the sum in range.
[[nodiscard]] constexpr auto operator+(
  const ByteOffset offset, const ByteCount count) noexcept -> ByteOffset
{
  return ByteOffset { offset.get() + count.get() };
}

//! Moves `offset` forward by `count` bytes.
constexpr auto operator+=(ByteOffset& offset, const ByteCount count) noexcept
  -> ByteOffset&
{
  offset.get() += count.get();
  return offset;
}

//! The number of bytes from `from` to `to`. The caller ensures `from <= to`.
[[nodiscard]] constexpr auto operator-(
  const ByteOffset to, const ByteOffset from) noexcept -> ByteCount
{
  return ByteCount { to.get() - from.get() };
}

//! The number of bytes in a span, as a ByteCount.
[[nodiscard]] constexpr auto CountOf(const size_t size) noexcept -> ByteCount
{
  return ByteCount { static_cast<uint64_t>(size) };
}

//! A half-open byte range: `count` bytes starting at `offset`.
struct ByteRange {
  ByteOffset offset { 0 };
  ByteCount count { 0 };

  //! A range whose end is representable; `value_too_large` otherwise.
  [[nodiscard]] static constexpr auto Make(const ByteOffset offset,
    const ByteCount count) noexcept -> Result<ByteRange>
  {
    if (count.get() > std::numeric_limits<uint64_t>::max() - offset.get()) {
      return ::oxygen::Err(std::make_error_code(std::errc::value_too_large));
    }
    return ::oxygen::Ok(ByteRange { .offset = offset, .count = count });
  }

  //! The first offset past the range.
  [[nodiscard]] constexpr auto End() const noexcept -> ByteOffset
  {
    return offset + count;
  }

  //! Whether `other` lies entirely within this range. An empty range is
  //! contained when its offset lies within this range or at its end.
  [[nodiscard]] constexpr auto Contains(const ByteRange other) const noexcept
    -> bool
  {
    return other.offset >= offset && other.offset.get() <= End().get()
      && other.count.get() <= End().get() - other.offset.get();
  }
};

//! What a device flush guarantees once it succeeds.
enum class Durability : uint8_t {
  kBuffered, //!< Bytes are handed to the OS; they survive the process.
  kStable, //!< Bytes are on stable storage; they survive a power loss.
};

//! What an open allows its handle to do.
enum class Access : uint8_t {
  kRead,
  kWrite,
  kReadWrite,
};

//! What an open does when the file exists or does not.
enum class Creation : uint8_t {
  kOpenExisting, //!< Fails when the file does not exist.
  kOpenAlways, //!< Opens the file, creating it empty when it does not exist.
  kCreateNew, //!< Creates the file; fails when it exists.
  kCreateAlways, //!< Creates the file, truncating it when it exists.
};

//! What other handles may do to a file while this one is open. POSIX has no
//! sharing modes, so only Windows enforces it.
enum class Sharing : uint8_t {
  kNone,
  kRead,
  kReadDelete, //!< Others read, and may replace or delete the file.
  kReadWriteDelete, //!< Others read, write, and may replace or delete it.
};

//! How the caller will access a file, so the OS can plan its caching.
enum class AccessHint : uint8_t {
  kRandom,
  kSequential,
};

//! Everything an open needs, by name (designated initializers).
struct OpenSpec {
  Access access = Access::kRead;
  Creation creation = Creation::kOpenExisting;
  Sharing sharing = Sharing::kReadDelete;
  AccessHint hint = AccessHint::kRandom;
  //! Bypass the OS cache (`FILE_FLAG_NO_BUFFERING`, `O_DIRECT`). Offsets,
  //! sizes and buffer addresses of every operation must then be multiples of
  //! the volume's sector size; `kIoAlignment` satisfies every common volume.
  bool unbuffered = false;
};

//! The alignment that serves unbuffered IO on every common volume.
inline constexpr size_t kIoAlignment = 4096;

} // namespace oxygen::serio
