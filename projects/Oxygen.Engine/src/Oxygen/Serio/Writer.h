//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Serio/AlignmentGuard.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Record.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::serio {

//! Abstract interface for binary data writers supporting type-erased access.
/*!
 Provides a non-template, type-erased interface for writing binary data to a
 stream. This allows code to interact with different concrete Writer
 implementations polymorphically, without knowing the underlying stream type.

 @note
   - AnyWriter is copyable and movable, but concrete implementations may
     restrict these operations, and should carefully consider the correctness of
     copying or moving their state.
   - Template-based writing (e.g., Store<T>()) is not part of this interface and
     must be accessed via concrete Writer types.
*/
class AnyWriter {
public:
  AnyWriter() = default;
  virtual ~AnyWriter() = default;

  OXYGEN_DEFAULT_COPYABLE(AnyWriter)
  OXYGEN_DEFAULT_MOVABLE(AnyWriter)

  [[nodiscard]] virtual auto WriteBlob(std::span<const std::byte> blob) noexcept
    -> Result<void> = 0;

  [[nodiscard]] auto WriteSequenceSize(const limits::SequenceSizeType size,
    const limits::SequenceSizeType max) noexcept -> Result<void>
  {
    if (size > max) {
      return ::oxygen::Err(make_error_code(Error::kTooLarge));
    }
    CHECK_RESULT(AlignTo(alignof(limits::SequenceSizeType)));
    CHECK_RESULT(Write(size));
    return {};
  }

  [[nodiscard]] virtual auto Position() const noexcept -> Result<size_t> = 0;

  [[nodiscard]] virtual auto AlignTo(size_t alignment) noexcept
    -> Result<void> = 0;
  [[nodiscard]] virtual auto ScopedAlignment(uint16_t alignment) noexcept(false)
    -> AlignmentGuard = 0;

  [[nodiscard]] virtual auto Flush() noexcept -> Result<void> = 0;

  template <typename T>
  [[nodiscard]] auto Write(const T& value) noexcept -> Result<void>
  {
    try {
      return Store(*this, value);
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "ADL specialization of Store failed: {}", ex.what());
      return ::oxygen::Err(std::errc::io_error);
    }
  }
};

//! Concrete binary writer for a specific stream type.
/*!
 Implements the AnyWriter interface for a concrete stream, providing efficient,
 type-safe binary serialization to the underlying stream.

 @tparam S Stream type implementing the Stream concept.

 ### Key Features

 - **Type-Erased Interface**: Inherits from AnyWriter, enabling polymorphic use
   with other writer types.
 - **Alignment Control**: Supports explicit alignment and scoped alignment
   guards for portable binary layouts.
 - **Direct Stream Access**: Writes directly to the provided stream reference.
 - **Error Handling**: All operations return Result types for robust error
   propagation.

 ### Usage Patterns

 Use Writer with any type satisfying the Stream concept to serialize data
 structures, arrays, and primitive types. Prefer Writer for performance and
 type safety when the stream type is known.

 ```cpp
 MemoryStream stream;
 Writer<MemoryStream> writer(stream);
 writer.Write(value);
 ```

 @see AnyWriter, Store, MemoryStream, AlignmentGuard
*/
template <OutputStream S> class Writer : protected Packer, public AnyWriter {
public:
  explicit Writer(S& stream) noexcept
    : stream_(stream)
  {
  }

  //! Start a scoped alignment guard with the specified alignment.
  [[nodiscard]] auto ScopedAlignment(const uint16_t alignment) noexcept(false)
    -> AlignmentGuard override
  {
    return AlignmentGuard(*this, alignment);
  }

  [[nodiscard]] auto WriteBlob(std::span<const std::byte> blob) noexcept
    -> Result<void> override
  {
    return stream_.get().Write(blob);
  }

  [[nodiscard]] auto Position() const noexcept -> Result<size_t> override
  {
    return ::oxygen::Ok(static_cast<size_t>(stream_.get().Position().get()));
  }

  /*!
   Aligns the stream position to the specified alignment boundary by adding
   padding (0x0) bytes if necessary. If a scoped alignment is active, it
   overrides the requested alignment. No action is taken if already aligned, and
   an effective alignment of 1 costs nothing.

   @param alignment The alignment boundary in bytes (power of two, 1 to 256).
   @return Result of the alignment operation; error if the alignment is invalid
           or the stream cannot be advanced or the position cannot be
           determined.

   @see ScopedAlignment, Reader
  */
  [[nodiscard]] auto AlignTo(size_t alignment) noexcept -> Result<void> override
  {
    size_t effective_alignment = alignment;
    if (const auto current = CurrentAlignment(); current != 0) {
      // override alignment with the top of the stack
      effective_alignment = current;
    }
    if (effective_alignment == 1) {
      return {};
    }
    if (effective_alignment == 0 || !std::has_single_bit(effective_alignment)
      || effective_alignment > kMaxAlignment) {
      return ::oxygen::Err(std::errc::invalid_argument);
    }

    const auto current_pos = stream_.get().Position().get();
    const size_t padding
      = (effective_alignment - (current_pos % effective_alignment))
      % effective_alignment;
    if (padding > 0) {
      constexpr std::array<std::byte, kMaxAlignment> kZeros {};
      return stream_.get().Write(std::span(kZeros).first(padding));
    }
    return {};
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void> override
  {
    return stream_.get().Flush();
  }

private:
  std::reference_wrapper<S> stream_;
};

//=== Store specializations ===-----------------------------------------------//

//! Serializes an integral value (except bool) as binary with platform
//! endianness.
/*!
 Encodes the value as a contiguous sequence of bytes, using the platform's
 endianness (little-endian on most systems; bytes are swapped if needed). The
 value is by default aligned in the stream according to its natural alignment,
 unless a specific packing directive is currently set with `ScopedAlignment()`.

 @tparam T Integral type (not bool)
 @param writer Writer to serialize to
 @param value Value to serialize
 @return Result of the write operation

 @see Store, AnyWriter
*/
template <typename T>
auto Store(AnyWriter& writer, T value) -> Result<void>
  requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
{
  static_assert(std::is_trivially_copyable_v<T>,
    "Type must be trivially copyable for binary serialization");
  static_assert(std::has_unique_object_representations_v<T>,
    "Type may have platform-dependent representation");
  CHECK_RESULT(writer.AlignTo(alignof(T)));
  if (!IsLittleEndian() && sizeof(T) > 1) {
    value = ByteSwap(value);
  }
  return writer.WriteBlob(
    // NOLINTNEXTLINE(*-reinterpret-cast)
    std::span(reinterpret_cast<const std::byte*>(&value), sizeof(T)));
}

//! Serializes a floating-point value as binary with platform endianness.
/*!
 Encodes the value as a contiguous sequence of bytes in IEEE-754 binary format,
 using the platform's endianness (little-endian on most systems; bytes are
 swapped if needed). The value is aligned in the stream according to its natural
 alignment, unless a specific packing directive is currently set with
 `ScopedAlignment()`.

 @tparam T Floating-point type
 @param writer Writer to serialize to
 @param value Value to serialize
 @return Result of the write operation

 @see Store, AnyWriter
*/
template <typename T>
auto Store(AnyWriter& writer, T value) -> Result<void>
  requires(std::is_floating_point_v<T>)
{
  static_assert(std::numeric_limits<T>::is_iec559,
    "Platform must use IEEE-754 floating point representation");
  CHECK_RESULT(writer.AlignTo(alignof(T)));
  if (!IsLittleEndian() && sizeof(T) > 1) {
    value = ByteSwap(value);
  }
  return writer.WriteBlob(
    // NOLINTNEXTLINE(*-reinterpret-cast)
    std::span(reinterpret_cast<const std::byte*>(&value), sizeof(T)));
}

//! Serializes an enum value via its underlying type.
template <typename T>
auto Store(AnyWriter& writer, const T value) -> Result<void>
  requires(std::is_enum_v<T>)
{
  return writer.Write(std::to_underlying(value));
}

//! Serializes a `bool` as one byte, 0 or 1. Only a `bool` binds here, so a
//! type without its own `Store` never converts to `bool` silently.
template <std::same_as<bool> T>
auto Store(AnyWriter& writer, const T value) -> Result<void>
{
  return writer.Write(static_cast<uint8_t>(value ? 1U : 0U));
}

//! Writes `value` as one copy of its bytes, aligned like any value to its
//! natural alignment unless a `ScopedAlignment()` is set.
/*!
 @see kIsRecord, RecordLayout, LoadRecord
*/
template <RecordLayout T>
auto StoreRecord(AnyWriter& writer, const T& value) -> Result<void>
{
  CHECK_RESULT(writer.AlignTo(alignof(T)));
  return writer.WriteBlob(std::as_bytes(std::span { &value, 1 }));
}

//! Serializes a record (`StoreRecord`).
template <Record T>
auto Store(AnyWriter& writer, const T& value) -> Result<void>
{
  static_assert(RecordLayout<T>,
    "a record must be trivially copyable, without padding, on a "
    "little-endian target");
  return StoreRecord(writer, value);
}

//! Serializes a std::string as a 32-bit length prefix followed by raw bytes.
/*!
 Encodes the string as a 32-bit unsigned length (platform endianness), followed
 by the UTF-8 bytes of the string (no null terminator). The length is aligned in
 the stream according to its natural alignment, unless a specific packing
 directive is currently set with `ScopedAlignment()`. No alignment or padding is
 added between the length and the string data.

 @param writer Writer to serialize to
 @param value String to serialize
 @return Result of the write operation

 @see Store, AnyWriter
*/
inline auto Store(AnyWriter& writer, const std::string& value) -> Result<void>
{
  if (value.length() > limits::kMaxStringLength) {
    return ::oxygen::Err(make_error_code(Error::kTooLarge));
  }

  // Write the length of the string, bounds-checked and properly aligned.
  CHECK_RESULT(writer.WriteSequenceSize(
    static_cast<limits::SequenceSizeType>(value.length()),
    limits::kMaxStringLength));

  // Write string data
  if (!value.empty()) {
    CHECK_RESULT(writer.WriteBlob(std::span(
      // NOLINTNEXTLINE(*-reinterpret-cast)
      reinterpret_cast<const std::byte*>(value.data()), value.length())));
  }
  return {};
}

//! Serializes a UUID as its raw 16-byte canonical binary payload.
/*!
 Writes exactly 16 bytes in UUID canonical byte order.
 Serialization is byte-stable and does not apply endian conversion.

 @param writer Writer to serialize to.
 @param value UUID value to serialize.
 @return Result of the write operation.

 @see Load(AnyReader&, Uuid&)
*/
inline auto Store(AnyWriter& writer, const Uuid& value) -> Result<void>
{
  auto pack = writer.ScopedAlignment(1);
  return writer.WriteBlob(oxygen::as_bytes(value));
}

namespace detail {
  //! Writes a sequence: its 32-bit length prefix, then its elements. Records
  //! and arithmetic elements are written as one copy after aligning once to
  //! their natural alignment; other elements one by one.
  template <typename T>
  auto StoreSequence(AnyWriter& writer, const std::span<const T> elements)
    -> Result<void>
  {
    if (elements.size() > limits::kMaxArrayLength) {
      return ::oxygen::Err(make_error_code(Error::kTooLarge));
    }
    CHECK_RESULT(writer.WriteSequenceSize(
      static_cast<limits::SequenceSizeType>(elements.size()),
      limits::kMaxArrayLength));
    if (elements.empty()) {
      return {};
    }
    if constexpr (BulkElement<T>) {
      CHECK_RESULT(writer.AlignTo(alignof(T)));
      return writer.WriteBlob(std::as_bytes(elements));
    } else {
      for (const auto& element : elements) {
        CHECK_RESULT(writer.Write(element));
      }
      return {};
    }
  }
} // namespace detail

//! Serializes a std::vector as a sequence (`detail::StoreSequence`).
template <typename T>
auto Store(AnyWriter& writer, const std::vector<T>& value) -> Result<void>
{
  if constexpr (std::is_same_v<T, bool>) {
    if (value.size() > limits::kMaxArrayLength) {
      return ::oxygen::Err(make_error_code(Error::kTooLarge));
    }
    CHECK_RESULT(writer.WriteSequenceSize(
      static_cast<limits::SequenceSizeType>(value.size()),
      limits::kMaxArrayLength));
    for (const bool element : value) {
      CHECK_RESULT(writer.Write(element));
    }
    return {};
  } else {
    return detail::StoreSequence(writer, std::span<const T> { value });
  }
}

//! Serializes a std::array as a sequence (`detail::StoreSequence`).
template <typename T, size_t N>
auto Store(AnyWriter& writer, const std::array<T, N>& value) -> Result<void>
{
  return detail::StoreSequence(writer, std::span<const T> { value });
}

//! Serializes a std::span as a sequence (`detail::StoreSequence`).
template <typename T, size_t Extent>
auto Store(AnyWriter& writer, const std::span<T, Extent> value) -> Result<void>
{
  return detail::StoreSequence(writer, std::span<const T> { value });
}

//! Serializes a std::optional: a `bool` that says whether a value follows,
//! then the value.
template <typename T>
auto Store(AnyWriter& writer, const std::optional<T>& value) -> Result<void>
{
  CHECK_RESULT(writer.Write(value.has_value()));
  if (value) {
    CHECK_RESULT(writer.Write(*value));
  }
  return {};
}

} // namespace oxygen::serio
