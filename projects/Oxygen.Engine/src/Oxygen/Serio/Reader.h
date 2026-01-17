//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <bit>
#include <functional>
#include <string>
#include <vector>

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AlignmentGuard.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::serio {

//! Abstract interface for binary data readers supporting type-erased access.
/*!
 Provides a non-template, type-erased interface for reading binary data from a
 stream. This allows code to interact with different concrete Reader
 implementations polymorphically, without knowing the underlying stream type.

 @note
   - AnyReader is copyable and movable, but concrete implementations may
     restrict these operations, and should carefully consider the correctness of
     copying or moving their state.
   - Template-based reading (e.g., read<T>()) is not part of this interface and
     must be accessed via concrete Reader types.
*/
class AnyReader {
public:
  AnyReader() = default;

  virtual ~AnyReader() = default;

  OXYGEN_DEFAULT_COPYABLE(AnyReader)
  OXYGEN_DEFAULT_MOVABLE(AnyReader)

  [[nodiscard]] virtual auto ReadBlob(size_t size) noexcept
    -> Result<std::vector<std::byte>>
    = 0;
  [[nodiscard]] virtual auto ReadBlobInto(std::span<std::byte> buffer) noexcept
    -> Result<void>
    = 0;

  [[nodiscard]] auto ReadSequenceSize(limits::SequenceSizeType& size,
    limits::SequenceSizeType max_size) noexcept -> Result<void>
  {
    CHECK_RESULT(AlignTo(alignof(limits::SequenceSizeType)));
    CHECK_RESULT(ReadInto(size));
    if (size > max_size) {
      size = 0;
      return ::oxygen::Err(std::errc::value_too_large);
    }

    return {};
  }

  [[nodiscard]] virtual auto Position() noexcept -> Result<size_t> = 0;

  [[nodiscard]] virtual auto AlignTo(size_t alignment) noexcept -> Result<void>
    = 0;
  [[nodiscard]] virtual auto ScopedAlignment(uint16_t alignment) noexcept(false)
    -> AlignmentGuard
    = 0;

  [[nodiscard]] virtual auto Forward(size_t num_bytes) noexcept -> Result<void>
    = 0;
  [[nodiscard]] virtual auto Seek(size_t pos) noexcept -> Result<void> = 0;

  template <typename T> [[nodiscard]] auto Read() noexcept -> Result<T>
  {
    T value;
    CHECK_RESULT(ReadInto(value));
    return ::oxygen::Ok(std::move(value));
  }

  template <typename T>
  [[nodiscard]] auto ReadInto(T& value) noexcept -> Result<void>
  {
    try {
      auto result = Load(*this, value);
      if (!result) {
        return ::oxygen::Err(result.error());
      }
      return {};
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "ADL specialization of Load failed: {}", ex.what());
      return ::oxygen::Err(std::errc::io_error);
    }
  }
};

//! Concrete binary reader for a specific stream type.
/*!
 Implements the AnyReader interface for a concrete stream, providing efficient,
 type-safe binary deserialization from the underlying stream.

 @tparam S Stream type implementing the Stream concept.

 ### Key Features

 - **Type-Erased Interface**: Inherits from AnyReader, enabling polymorphic use
   with other reader types.
 - **Alignment Control**: Supports explicit alignment and scoped alignment
   guards for portable binary layouts.
 - **Direct Stream Access**: Reads directly from the provided stream reference.
 - **Error Handling**: All operations return Result types for robust error
   propagation.

 ### Usage Patterns

 Use Reader with any type satisfying the Stream concept to deserialize data
 structures, arrays, and primitive types. Prefer Reader for performance and type
 safety when the stream type is known.

 ```cpp
 MemoryStream stream;
 Reader<MemoryStream> reader(stream);
 int value = reader.Read<int>().value();
 ```

 @see AnyReader, Load, MemoryStream, AlignmentGuard
*/
template <Stream S> class Reader : protected Packer, public AnyReader {
public:
  explicit Reader(S& stream) noexcept
    : stream_(stream)
  {
  }

  ~Reader() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Reader)
  OXYGEN_MAKE_NON_MOVABLE(Reader)

  //! Start a scoped alignment guard with the specified alignment.
  [[nodiscard]] auto ScopedAlignment(const uint16_t alignment) noexcept(false)
    -> AlignmentGuard override
  {
    return AlignmentGuard(*this, alignment);
  }

  [[nodiscard]] auto ReadBlob(size_t size) noexcept
    -> Result<std::vector<std::byte>> override
  {
    if (size == 0) {
      return ::oxygen::Ok(std::vector<std::byte> {});
    }
    std::vector<std::byte> buffer(size);
    auto result = stream_.get().Read(buffer.data(), size);
    if (!result) {
      return ::oxygen::Err(result.error());
    }
    return ::oxygen::Ok(std::move(buffer));
  }

  [[nodiscard]] auto ReadBlobInto(std::span<std::byte> buffer) noexcept
    -> Result<void> override
  {
    return stream_.get().Read(buffer.data(), buffer.size());
  }

  [[nodiscard]] auto Position() noexcept -> Result<size_t> override
  {
    return stream_.get().Position();
  }

  /*!
   Aligns the stream position to the specified alignment boundary by skipping
   padding bytes if necessary. If a scoped alignment is active, it overrides
   the requested alignment. No action is taken if already aligned.

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
    if (effective_alignment == 0
      || !std::has_single_bit(effective_alignment)
      || effective_alignment > kMaxAlignment) {
      return ::oxygen::Err(std::errc::invalid_argument);
    }

    const auto current_pos = stream_.get().Position();
    if (!current_pos) {
      return ::oxygen::Err(current_pos.error());
    }

    const size_t padding
      = (effective_alignment
          - (current_pos.value() % effective_alignment))
      % effective_alignment;
    if (padding > 0) {
      return stream_.get().Forward(padding);
    }
    return {};
  }

  [[nodiscard]] auto Forward(size_t num_bytes) noexcept -> Result<void> override
  {
    return stream_.get().Forward(num_bytes);
  }

  [[nodiscard]] auto Seek(size_t pos) noexcept -> Result<void> override
  {
    return stream_.get().Seek(pos);
  }

private:
  std::reference_wrapper<S> stream_;
};

//! Deserializes an integral value (except bool) from the stream.
/*!
 Reads an integral value (except bool) from the stream as a contiguous sequence
 of bytes, using the platform's endianness (little-endian on most systems; bytes
 are swapped if needed). The value is aligned in the stream according to its
 natural alignment, unless a specific packing directive is currently set with
 `ScopedAlignment()`.

 @tparam T Integral type (not bool)
 @param reader Reader to deserialize from
 @param value Reference to store the deserialized value
 @return Result of the read operation

 @see Load, AnyReader
*/
template <typename T>
auto Load(AnyReader& reader, T& value) -> Result<void>
  requires(std::is_integral_v<T> &&
    // bool is not safe for binary serialization - use uint8_t
    !std::is_same_v<T, bool>)
{
  static_assert(std::is_trivially_copyable_v<T>,
    "Type must be trivially copyable for binary serialization");
  static_assert(std::has_unique_object_representations_v<T>,
    "Type may have platform-dependent representation");
  CHECK_RESULT(reader.AlignTo(alignof(T)));
  CHECK_RESULT(
    reader.ReadBlobInto(std::span(reinterpret_cast<std::byte*>(&value),
      sizeof(T)))); // NOLINT(*-reinterpret-cast)

  if (!IsLittleEndian() && sizeof(T) > 1) {
    value = ByteSwap(value);
  }

  return {};
}

//! Deserializes a floating-point value from the stream.
/*!
 Reads a floating-point value from the stream as a contiguous sequence of bytes
 in IEEE-754 binary format, using the platform's endianness (little-endian on
 most systems; bytes are swapped if needed). The value is aligned in the stream
 according to its natural alignment, unless a specific packing directive is
 currently set with `ScopedAlignment()`.

 @tparam T Floating-point type
 @param reader Reader to deserialize from
 @param value Reference to store the deserialized value
 @return Result of the read operation

 @note Requires IEEE-754 floating point representation.
 @see Load, AnyReader
*/
template <typename T>
auto Load(AnyReader& reader, T& value) -> Result<void>
  requires(std::is_floating_point_v<T>)
{
  static_assert(std::numeric_limits<T>::is_iec559,
    "Platform must use IEEE-754 floating point representation");
  CHECK_RESULT(reader.AlignTo(alignof(T)));
  CHECK_RESULT(
    reader.ReadBlobInto(std::span(reinterpret_cast<std::byte*>(&value),
      sizeof(T)))); // NOLINT(*-reinterpret-cast)

  if (!IsLittleEndian() && sizeof(T) > 1) {
    value = ByteSwap(value);
  }

  return {};
}

//! Deserializes a std::string from the stream.
/*!
 Reads a std::string from the stream as a 32-bit length prefix (platform
 endianness), followed by the UTF-8 bytes of the string (no null terminator).
 The length is aligned in the stream according to its natural alignment, unless
 a specific packing directive is currently set with `ScopedAlignment()`. No
 alignment is considered between the length and the string data.

 @param reader Reader to deserialize from
 @param value Reference to store the deserialized string
 @return Result of the read operation

 @note The function will clear the string if the length is zero.
 @see Load, AnyReader
*/
inline auto Load(AnyReader& reader, std::string& value) noexcept -> Result<void>
{
  limits::SequenceSizeType length { 0 };
  CHECK_RESULT(reader.ReadSequenceSize(length, limits::kMaxStringLength));
  if (length == 0) {
    value.clear();
    return {};
  }
  // Standard C++ strings do not require a manual null terminator
  value.resize(length);
  CHECK_RESULT(reader.ReadBlobInto(
    // NOLINTNEXTLINE(*-reinterpret-cast)
    std::span(reinterpret_cast<std::byte*>(value.data()), length)));
  return {};
}

//! Deserializes a std::vector from the stream.
/*!
 Reads a std::vector from the stream as a 32-bit length prefix (platform
 endianness), followed by each element in sequence. The length and each element
 are aligned in the stream according to their natural alignment, unless a
 specific packing directive is currently set with `ScopedAlignment()`.

 @tparam T Element type
 @param reader Reader to deserialize from
 @param value Reference to store the deserialized vector
 @return Result of the read operation

 @note The function will clear the vector if the length is zero.
 @see Load, AnyReader
*/
template <typename T>
auto Load(AnyReader& reader, std::vector<T>& value) -> Result<void>
{
  limits::SequenceSizeType length = 0;
  CHECK_RESULT(reader.ReadSequenceSize(length, limits::kMaxArrayLength));
  if (length == 0) {
    value.clear();
    return {};
  }

  // Align for array elements if needed
  if constexpr (sizeof(T) > 1) {
    CHECK_RESULT(reader.AlignTo(alignof(T)));
  }

  value.clear();
  value.reserve(length);

  for (limits::SequenceSizeType i = 0; i < length; ++i) {
    T element;
    CHECK_RESULT(reader.ReadInto(element));
    value.push_back(std::move(element));
  }

  return {};
}

} // namespace oxygen::serio
