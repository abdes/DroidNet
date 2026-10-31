//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Serio/AlignmentGuard.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Record.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Types.h>

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

  //! Reads `size` bytes into a new buffer; `kTooLarge`, before allocating,
  //! when fewer bytes remain.
  [[nodiscard]] virtual auto ReadBlob(size_t size) noexcept
    -> Result<std::vector<std::byte>> = 0;
  [[nodiscard]] virtual auto ReadBlobInto(std::span<std::byte> buffer) noexcept
    -> Result<void> = 0;

  //! Reads a sequence's length prefix; `kTooLarge` when it exceeds
  //! `max_size`, or when `size` elements of at least `min_element_size`
  //! bytes each cannot fit in the bytes that remain.
  [[nodiscard]] auto ReadSequenceSize(limits::SequenceSizeType& size,
    const limits::SequenceSizeType max_size,
    const size_t min_element_size = 1) noexcept -> Result<void>
  {
    CHECK_RESULT(AlignTo(alignof(limits::SequenceSizeType)));
    CHECK_RESULT(ReadInto(size));
    if (size > max_size
      || uint64_t { size } * min_element_size > Remaining().get()) {
      size = 0;
      return ::oxygen::Err(make_error_code(Error::kTooLarge));
    }

    return {};
  }

  [[nodiscard]] virtual auto Position() noexcept -> Result<size_t> = 0;

  //! The bytes between the cursor and the end of what this reader may read.
  [[nodiscard]] virtual auto Remaining() noexcept -> ByteCount = 0;

  [[nodiscard]] virtual auto AlignTo(size_t alignment) noexcept
    -> Result<void> = 0;
  [[nodiscard]] virtual auto ScopedAlignment(uint16_t alignment) noexcept(false)
    -> AlignmentGuard = 0;

  [[nodiscard]] virtual auto Forward(size_t num_bytes) noexcept
    -> Result<void> = 0;
  [[nodiscard]] virtual auto Seek(size_t pos) noexcept -> Result<void> = 0;

  template <typename T> [[nodiscard]] auto Read() noexcept -> Result<T>
  {
    T value {};
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
template <InputStream S> class Reader : protected Packer, public AnyReader {
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
    if (CountOf(size) > Remaining()) {
      return ::oxygen::Err(make_error_code(Error::kTooLarge));
    }
    std::vector<std::byte> buffer(size);
    auto result = ReadExactly(stream_.get(), std::span(buffer));
    if (!result) {
      return ::oxygen::Err(result.error());
    }
    return ::oxygen::Ok(std::move(buffer));
  }

  [[nodiscard]] auto ReadBlobInto(std::span<std::byte> buffer) noexcept
    -> Result<void> override
  {
    return ReadExactly(stream_.get(), buffer);
  }

  [[nodiscard]] auto Position() noexcept -> Result<size_t> override
  {
    return ::oxygen::Ok(static_cast<size_t>(stream_.get().Position().get()));
  }

  [[nodiscard]] auto Remaining() noexcept -> ByteCount override
  {
    const auto& stream = stream_.get();
    return ByteCount { stream.Size().get() - stream.Position().get() };
  }

  /*!
   Aligns the stream position to the specified alignment boundary by skipping
   padding bytes if necessary. If a scoped alignment is active, it overrides
   the requested alignment. No action is taken if already aligned, and an
   effective alignment of 1 costs nothing.

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
      return Skip(stream_.get(), CountOf(padding));
    }
    return {};
  }

  [[nodiscard]] auto Forward(size_t num_bytes) noexcept -> Result<void> override
  {
    return Skip(stream_.get(), CountOf(num_bytes));
  }

  [[nodiscard]] auto Seek(size_t pos) noexcept -> Result<void> override
  {
    return stream_.get().Seek(ByteOffset { pos });
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
  CHECK_RESULT(reader.ReadBlobInto(
    // NOLINTNEXTLINE(*-reinterpret-cast)
    std::span(reinterpret_cast<std::byte*>(&value), sizeof(T))));

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
  CHECK_RESULT(reader.ReadBlobInto(
    // NOLINTNEXTLINE(*-reinterpret-cast)
    std::span(reinterpret_cast<std::byte*>(&value), sizeof(T))));

  if (!IsLittleEndian() && sizeof(T) > 1) {
    value = ByteSwap(value);
  }

  return {};
}

//! Deserializes an enum value from the stream via its underlying type.
/*!
 Reads the enum's underlying integral representation and casts it back to
 * the
 enum type.
*/
template <typename T>
auto Load(AnyReader& reader, T& value) -> Result<void>
  requires(std::is_enum_v<T>)
{
  using RawType = std::underlying_type_t<T>;
  RawType raw_value {};
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<T>(raw_value);
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

//! Deserializes and validates a UUIDv7 from its raw 16-byte canonical payload.
/*!
 Reads exactly 16 bytes in UUID canonical byte order, then validates the

 * payload through `Uuid::FromBytes`. Serialization is byte-stable and does not

 * apply endian conversion.

 @param reader Reader to deserialize from.
 @param
 * value UUID output value.
 @return Result of the read operation.

 @see
 * Store(AnyWriter&, const Uuid&)
*/
inline auto Load(AnyReader& reader, Uuid& value) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  auto bytes = std::array<std::uint8_t, Uuid::kSize> {};
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { bytes })));

  const auto parsed = Uuid::FromBytes(bytes);
  if (!parsed.has_value()) {
    return ::oxygen::Err(parsed.error());
  }

  value = parsed.value();
  return {};
}

//! Deserializes a `bool` stored as one byte, 0 or 1; any other byte fails with
//! `invalid_argument`.
inline auto Load(AnyReader& reader, bool& value) -> Result<void>
{
  uint8_t raw = 0;
  CHECK_RESULT(reader.ReadInto(raw));
  if (raw > 1) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  value = raw == 1;
  return {};
}

//! Reads `value` as one copy of its bytes, aligned like any value to its
//! natural alignment unless a `ScopedAlignment()` is set.
/*!
 Records load through it. A type that checks its values after reading them is
 not declared a record, so that sequences of it never skip the check; its own
 `Load` calls `LoadRecord`, then checks.

 @see kIsRecord, RecordLayout, StoreRecord
*/
template <RecordLayout T>
auto LoadRecord(AnyReader& reader, T& value) -> Result<void>
{
  CHECK_RESULT(reader.AlignTo(alignof(T)));
  return reader.ReadBlobInto(std::as_writable_bytes(std::span { &value, 1 }));
}

//! Deserializes a record (`LoadRecord`).
template <Record T> auto Load(AnyReader& reader, T& value) -> Result<void>
{
  static_assert(RecordLayout<T>,
    "a record must be trivially copyable, without padding, on a "
    "little-endian target");
  return LoadRecord(reader, value);
}

namespace detail {
  //! Reads the elements of a sequence whose length is already read: bulk
  //! elements as one copy after aligning once, others one by one.
  template <typename T>
  auto LoadElements(AnyReader& reader, std::span<T> elements) -> Result<void>
  {
    if (elements.empty()) {
      return {};
    }
    if constexpr (BulkElement<T>) {
      CHECK_RESULT(reader.AlignTo(alignof(T)));
      return reader.ReadBlobInto(std::as_writable_bytes(elements));
    } else {
      for (auto& element : elements) {
        CHECK_RESULT(reader.ReadInto(element));
      }
      return {};
    }
  }

  //! The smallest number of bytes an element of type `T` takes in a stream.
  template <typename T>
  constexpr auto kMinEncodedSize = BulkElement<T> ? sizeof(T) : size_t { 1 };
} // namespace detail

//! Deserializes a std::vector from the stream.
/*!
 Reads a 32-bit length prefix, aligned to its natural alignment, followed by
 the elements. Records and arithmetic elements are read as one copy after
 aligning once to their natural alignment; other elements are read one by
 one. The length is checked against the bytes that remain before anything is
 allocated, and fails with `kTooLarge`.

 @tparam T Element type
 @param reader Reader to deserialize from
 @param value Reference to store the deserialized vector
 @return Result of the read operation

 @see Load, AnyReader
*/
template <typename T>
auto Load(AnyReader& reader, std::vector<T>& value) -> Result<void>
{
  limits::SequenceSizeType length = 0;
  CHECK_RESULT(reader.ReadSequenceSize(
    length, limits::kMaxArrayLength, detail::kMinEncodedSize<T>));
  value.clear();
  if constexpr (std::is_same_v<T, bool>) {
    for (limits::SequenceSizeType index = 0; index < length; ++index) {
      bool element = false;
      CHECK_RESULT(reader.ReadInto(element));
      value.push_back(element);
    }
    return {};
  } else {
    value.resize(length);
    return detail::LoadElements(reader, std::span<T> { value });
  }
}

//! Deserializes a std::array, stored like a vector; a stored length other
//! than `N` fails with `invalid_argument`.
template <typename T, size_t N>
auto Load(AnyReader& reader, std::array<T, N>& value) -> Result<void>
{
  limits::SequenceSizeType length = 0;
  CHECK_RESULT(reader.ReadSequenceSize(
    length, limits::kMaxArrayLength, detail::kMinEncodedSize<T>));
  if (length != N) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return detail::LoadElements(reader, std::span<T> { value });
}

//! Deserializes a std::optional: a `bool` that says whether a value follows,
//! then the value.
template <typename T>
auto Load(AnyReader& reader, std::optional<T>& value) -> Result<void>
{
  bool has_value = false;
  CHECK_RESULT(reader.ReadInto(has_value));
  if (!has_value) {
    value.reset();
    return {};
  }
  CHECK_RESULT(reader.ReadInto(value.emplace()));
  return {};
}

} // namespace oxygen::serio
