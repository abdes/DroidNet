//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <bit>
#include <functional>
#include <span>
#include <string_view>

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Serio/AlignmentGuard.h>
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
    -> Result<void>
    = 0;

  [[nodiscard]] auto WriteSequenceSize(const limits::SequenceSizeType size,
    const limits::SequenceSizeType max) noexcept -> Result<void>
  {
    if (size > max) {
      return ::oxygen::Err(std::errc::value_too_large);
    }
    CHECK_RESULT(AlignTo(alignof(limits::SequenceSizeType)));
    CHECK_RESULT(Write(size));
    return {};
  }

  [[nodiscard]] virtual auto Position() const noexcept -> Result<size_t> = 0;

  [[nodiscard]] virtual auto AlignTo(size_t alignment) noexcept -> Result<void>
    = 0;
  [[nodiscard]] virtual auto ScopedAlignment(uint16_t alignment) noexcept(false)
    -> AlignmentGuard
    = 0;

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
template <Stream S> class Writer : protected Packer, public AnyWriter {
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
    return stream_.get().Write(blob.data(), blob.size());
  }

  [[nodiscard]] auto Position() const noexcept -> Result<size_t> override
  {
    return stream_.get().Position();
  }

  /*!
   Aligns the stream position to the specified alignment boundary by adding
   padding (0x0) bytes if necessary. If a scoped alignment is active, it
   overrides the requested alignment. No action is taken if already aligned.

   @param alignment The alignment boundary in bytes (must be a power of two).
   @return Result of the alignment operation; error if the stream cannot be
           advanced or the position cannot be determined.

   @see ScopedAlignment, Reader
  */
  [[nodiscard]] auto AlignTo(size_t alignment) noexcept -> Result<void> override
  {
    if (const auto current = CurrentAlignment(); current != 0) {
      // override alignment with the top of the stack
      alignment = current;
    }

    const auto current_pos = stream_.get().Position();
    if (!current_pos) {
      return ::oxygen::Err(current_pos.error());
    }

    const size_t padding
      = (alignment - (current_pos.value() % alignment)) % alignment;
    if (padding > 0) {
      std::array<std::byte, kMaxAlignment> zeros { std::byte { 0 } };
      return stream_.get().Write(zeros.data(), padding);
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
    std::span(reinterpret_cast<const std::byte*>(&value), sizeof(T)));
}

//! Serializes trivially copyable POD types by raw byte copy.
/*! Aligns to the natural alignment of T and writes the in-memory
    representation without endianness conversion. Intended for packed
    structs used in asset formats (e.g., PAK descriptors).

 @tparam T Trivially copyable non-arithmetic POD type
 @param writer Writer to serialize to
 @param value Value to serialize
 @return Result of the write operation

 @see Store, AnyWriter
*/
template <typename T>
auto Store(AnyWriter& writer, const T& value) -> Result<void>
  requires(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>
    && !std::is_integral_v<T> && !std::is_floating_point_v<T>)
{
  static_assert(std::has_unique_object_representations_v<T>,
    "Type must have a unique object representation for binary serialization");
  CHECK_RESULT(writer.AlignTo(alignof(T)));
  const auto bytes = std::as_bytes(std::span(&value, static_cast<size_t>(1)));
  return writer.WriteBlob(bytes);
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
    return ::oxygen::Err(std::errc::value_too_large);
  }

  // Write the length of the string, bounds-checked and properly aligned.
  CHECK_RESULT(writer.WriteSequenceSize(
    static_cast<limits::SequenceSizeType>(value.length()),
    limits::kMaxStringLength));

  // Write string data
  if (!value.empty()) {
    CHECK_RESULT(writer.WriteBlob(std::span(
      reinterpret_cast<const std::byte*>(value.data()), value.length())));
  }
  return {};
}

//! Serializes a std::vector as a 32-bit length prefix followed by each element.
/*!
 Encodes the vector as a 32-bit unsigned length (platform endianness), followed
 by each element encoded in sequence. The length and each element are aligned in
 the stream according to its natural alignment, unless a specific packing
 directive is currently set with `ScopedAlignment()`.

 @tparam T Element type
 @param writer Writer to serialize to
 @param value Vector to serialize
 @return Result of the write operation

 @see Store, AnyWriter
*/
template <typename T>
auto Store(AnyWriter& writer, const std::vector<T>& value) -> Result<void>
{
  if (value.size() > limits::kMaxArrayLength) {
    return ::oxygen::Err(std::errc::message_size);
  }

  // Write the size of the array, bounds-checked and properly aligned.
  CHECK_RESULT(writer.WriteSequenceSize(
    static_cast<limits::SequenceSizeType>(value.size()),
    limits::kMaxArrayLength));

  // Align for array elements if needed
  if constexpr (sizeof(T) > 1) {
    CHECK_RESULT(writer.AlignTo(alignof(T)));
  }

  // Write array data
  for (const auto& item : value) {
    CHECK_RESULT(writer.Write(item));
  }

  return {};
}

//! Serializes a std::array as a 32-bit length prefix followed by each element.
/*!
 Encodes the array as a 32-bit unsigned length (platform endianness), followed
 by each element encoded in sequence. The length and each element are aligned in
 the stream according to its natural alignment, unless a specific packing
 directive is currently set with `ScopedAlignment()`.

 @tparam T Element type
 @tparam N Array size
 @param writer Writer to serialize to
 @param value Array to serialize
 @return Result of the write operation

 @see Store, AnyWriter
*/
template <typename T, std::size_t N>
auto Store(AnyWriter& writer, const std::array<T, N>& value) -> Result<void>
{
  if (N > limits::kMaxArrayLength) {
    return std::make_error_code(std::errc::message_size);
  }

  // Write the size of the array, bounds-checked and properly aligned.
  CHECK_RESULT(writer.WriteSequenceSize(
    static_cast<limits::SequenceSizeType>(N), limits::kMaxArrayLength));

  // Align for array elements if needed
  if constexpr (sizeof(T) > 1) {
    CHECK_RESULT(writer.AlignTo(alignof(T)));
  }

  // Write array data
  for (const auto& item : value) {
    CHECK_RESULT(writer.Write(item));
  }
}

//! Serializes a std::span as a 32-bit length prefix followed by each element.
/*!
 Encodes the span as a 32-bit unsigned length (platform endianness), followed
 by each element encoded in sequence. The length and each element are aligned in
 the stream according to their natural alignment, unless a specific packing
 directive is currently set with `ScopedAlignment()`.

 @tparam T Element type
 @param writer Writer to serialize to
 @param value Span to serialize
 @return Result of the write operation

 @see Store, AnyWriter
*/
template <typename T>
auto Store(AnyWriter& writer, const std::span<T>& value) -> Result<void>
{
  if (value.size() > limits::kMaxArrayLength) {
    return std::make_error_code(std::errc::message_size);
  }

  // Write the size of the array, bounds-checked and properly aligned.
  CHECK_RESULT(writer.WriteSequenceSize(
    static_cast<limits::SequenceSizeType>(value.size()),
    limits::kMaxArrayLength));

  // Align for array elements if needed
  if constexpr (sizeof(T) > 1) {
    CHECK_RESULT(writer.AlignTo(alignof(T)));
  }

  // Write array data
  for (const auto& item : value) {
    CHECK_RESULT(writer.Write(item));
  }

  return {};
}

} // namespace oxygen::serio
