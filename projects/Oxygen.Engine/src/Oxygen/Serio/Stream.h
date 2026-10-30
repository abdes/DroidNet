//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! A cursor that reads bytes sequentially, and can move within its bytes.
/*!
 - `ReadSome` reads up to `out.size()` bytes at the cursor and advances it;
   it returns zero bytes only at the end.
 - `Position()` and `Size()` cannot fail: a stream knows both.
 - `Seek` moves the cursor to any offset up to `Size()`; past it, it fails
   with `kOutOfBounds` and leaves the cursor where it was.

 @see ReadExactly, Skip
*/
template <class S>
concept InputStream = requires(S& s, std::span<std::byte> out, ByteOffset at) {
  { s.ReadSome(out) } noexcept -> std::same_as<Result<ByteCount>>;
  { s.Position() } noexcept -> std::same_as<ByteOffset>;
  { s.Size() } noexcept -> std::same_as<ByteCount>;
  { s.Seek(at) } noexcept -> std::same_as<Result<void>>;
};

//! A cursor that writes bytes sequentially.
/*!
 - `Write` writes all of `in` at the cursor and advances it, or fails.
 - `Flush` hands buffered bytes to the stream's destination.
 - `Position()` is the number of bytes written since the stream's start.
*/
template <class S>
concept OutputStream = requires(S& s, std::span<const std::byte> in) {
  { s.Write(in) } noexcept -> std::same_as<Result<void>>;
  { s.Flush() } noexcept -> std::same_as<Result<void>>;
  { s.Position() } noexcept -> std::same_as<ByteOffset>;
};

//! An output stream that can overwrite bytes it has already written, such as
//! a header completed once its content is known.
/*!
 `PatchAt` writes `in` at `at` without moving the cursor, and fails with
 `kOutOfBounds` when `at + in.size()` passes `Position()`.
*/
template <class S>
concept PatchableOutputStream = OutputStream<S>
  && requires(S& s, ByteOffset at, std::span<const std::byte> in) {
       { s.PatchAt(at, in) } noexcept -> std::same_as<Result<void>>;
     };

//! Fills `out` from `stream`, or fails with `kEndOfData` when the stream ends
//! first; the bytes before the end are consumed.
template <InputStream S>
[[nodiscard]] auto ReadExactly(S& stream, std::span<std::byte> out) noexcept
  -> Result<void>
{
  while (!out.empty()) {
    const auto read = stream.ReadSome(out);
    if (!read) {
      return ::oxygen::Err(read.error());
    }
    if (read->get() == 0) {
      return ::oxygen::Err(make_error_code(Error::kEndOfData));
    }
    out = out.subspan(static_cast<size_t>(read->get()));
  }
  return {};
}

//! Moves the cursor of `stream` forward by `count` bytes; `kOutOfBounds` when
//! that passes the end.
template <InputStream S>
[[nodiscard]] auto Skip(S& stream, const ByteCount count) noexcept
  -> Result<void>
{
  const auto position = stream.Position();
  if (count.get() > stream.Size().get() - position.get()) {
    return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
  }
  return stream.Seek(position + count);
}

//! Serialization limits for serio sequences and strings.
/*!
 - **SequenceSizeType**: the type of array and string length prefixes.
 - **kMaxStringLength**: the longest string a reader accepts (1 MiB).
 - **kMaxArrayLength**: the longest array a reader accepts (1 Mi elements).
*/
namespace limits {
  using SequenceSizeType = uint32_t;
  [[maybe_unused]] constexpr SequenceSizeType kMaxStringLength
    = 1024ULL * 1024; // 1MB
  [[maybe_unused]] constexpr SequenceSizeType kMaxArrayLength
    = 1024ULL * 1024; // 1MB
}

} // namespace oxygen::serio

//! User-defined literal operator for `std::byte` literals.
/*!
 For character literals: 'x'_b
*/
constexpr auto operator""_b(char c) noexcept -> std::byte
{
  return static_cast<std::byte>(c);
}

//! User-defined literal operator for `std::byte` literals.
/*!
 For integer literals: 0xFF_b

 @throw std::out_of_range if the value exceeds the range of a byte (0x00 to
 0xFF). Use compile time `consteval`.

 @note The C++ standard mandates that user-defined literals can only be defined
 for `unsigned long long`, so we use that type for the literal operator.
*/
consteval auto operator""_b(unsigned long long n) -> std::byte
{
  // Compile-time check: only allow values that fit in a byte
  if (n > std::numeric_limits<unsigned char>::max()) {
    throw std::out_of_range("byte literal out of range (must be <= 0xFF)");
  }
  return static_cast<std::byte>(n);
}
