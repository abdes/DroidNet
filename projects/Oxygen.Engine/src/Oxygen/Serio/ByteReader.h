//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Record.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! A bounds-checked cursor that parses bytes already in memory.
/*!
 Reads copy records and arithmetic values out of the bytes, so no object is
 ever aliased from raw memory; `ReadBytes` and `ReadStringView` return views
 of the bytes themselves, valid as long as the bytes are. Nothing is virtual
 and nothing allocates.

 A read that needs more bytes than remain fails with `kEndOfData` and leaves
 the cursor where it was.

 ```cpp
 auto reader = ByteReader { mapped.Bytes() };
 const auto header = reader.Read<IndexHeader>();
 const auto names = reader.ReadStringView(CountOf(header->names_size));
 ```

 @see Record, BulkElement
*/
class ByteReader {
public:
  explicit ByteReader(const std::span<const std::byte> bytes) noexcept
    : bytes_(bytes)
  {
  }

  //! The cursor, in bytes from the start of the span.
  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return ByteOffset { position_ };
  }

  //! The size of the span.
  [[nodiscard]] auto Size() const noexcept -> ByteCount
  {
    return CountOf(bytes_.size());
  }

  //! The bytes between the cursor and the end.
  [[nodiscard]] auto Remaining() const noexcept -> ByteCount
  {
    return CountOf(bytes_.size() - position_);
  }

  //! Moves the cursor to `at`; `kOutOfBounds` past the end.
  [[nodiscard]] auto Seek(const ByteOffset at) noexcept -> Result<void>
  {
    if (at.get() > bytes_.size()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    position_ = static_cast<size_t>(at.get());
    return {};
  }

  //! Moves the cursor forward by `count`; `kOutOfBounds` past the end.
  [[nodiscard]] auto Skip(const ByteCount count) noexcept -> Result<void>
  {
    if (count > Remaining()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    position_ += static_cast<size_t>(count.get());
    return {};
  }

  //! Moves the cursor to the next multiple of `alignment` from the start of
  //! the span; `kOutOfBounds` past the end.
  [[nodiscard]] auto AlignTo(const size_t alignment) noexcept -> Result<void>
  {
    if (alignment == 0 || !std::has_single_bit(alignment)) {
      return ::oxygen::Err(std::make_error_code(std::errc::invalid_argument));
    }
    const auto padding = (alignment - (position_ % alignment)) % alignment;
    return Skip(CountOf(padding));
  }

  //! Copies a record or an arithmetic value at the cursor.
  template <BulkElement T> [[nodiscard]] auto Read() noexcept -> Result<T>
  {
    if constexpr (Record<T>) {
      static_assert(RecordLayout<T>,
        "a record must be trivially copyable, without padding, on a "
        "little-endian target");
    }
    const auto bytes = Take(sizeof(T));
    if (!bytes) {
      return ::oxygen::Err(bytes.error());
    }
    T value {};
    std::memcpy(&value, bytes->data(), sizeof(T));
    return ::oxygen::Ok(value);
  }

  //! A view of the next `count` bytes.
  [[nodiscard]] auto ReadBytes(const ByteCount count) noexcept
    -> Result<std::span<const std::byte>>
  {
    if (count > Remaining()) {
      return ::oxygen::Err(make_error_code(Error::kEndOfData));
    }
    return Take(static_cast<size_t>(count.get()));
  }

  //! A view of the next `count` bytes as characters.
  [[nodiscard]] auto ReadStringView(const ByteCount count) noexcept
    -> Result<std::string_view>
  {
    const auto bytes = ReadBytes(count);
    if (!bytes) {
      return ::oxygen::Err(bytes.error());
    }
    return ::oxygen::Ok(std::string_view(
      // NOLINTNEXTLINE(*-reinterpret-cast)
      reinterpret_cast<const char*>(bytes->data()), bytes->size()));
  }

private:
  [[nodiscard]] auto Take(const size_t size) noexcept
    -> Result<std::span<const std::byte>>
  {
    if (size > bytes_.size() - position_) {
      return ::oxygen::Err(make_error_code(Error::kEndOfData));
    }
    const auto taken = bytes_.subspan(position_, size);
    position_ += size;
    return ::oxygen::Ok(taken);
  }

  std::span<const std::byte> bytes_;
  size_t position_ { 0 };
};

//! Copies the record or arithmetic value at `at` in `bytes`.
/*!
 The one-read form of `ByteReader`, for a value whose offset is known.

 @return The value; `kOutOfBounds` when `at` is past the end, `kEndOfData`
   when the value does not fit before it.
*/
template <BulkElement T>
[[nodiscard]] auto CopyAt(const std::span<const std::byte> bytes,
  const ByteOffset at = ByteOffset { 0 }) noexcept -> Result<T>
{
  auto reader = ByteReader { bytes };
  if (const auto seek = reader.Seek(at); !seek) {
    return ::oxygen::Err(seek.error());
  }
  return reader.Read<T>();
}

} // namespace oxygen::serio
