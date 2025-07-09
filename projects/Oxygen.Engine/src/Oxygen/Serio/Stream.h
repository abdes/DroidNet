//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>

namespace oxygen::serio {

//! Concept to specify a stream that can be written to and read from.
/*!
 @note All methods should be noexcept.
*/
template <typename T>
concept Stream = requires(T t, const std::byte* cdata, std::byte* data,
  std::span<const std::byte> sdata, size_t size) {
  { t.Read(data, size) } -> std::same_as<Result<void>>;
  { t.Write(cdata, size) } -> std::same_as<Result<void>>;
  { t.Write(sdata) } -> std::same_as<Result<void>>;
  { t.Flush() } -> std::same_as<Result<void>>;
  { t.Size() } -> std::same_as<Result<size_t>>;
  { t.Position() } -> std::same_as<Result<size_t>>;
  { t.Seek(size) } -> std::same_as<Result<void>>;
  { t.Backward(size) } -> std::same_as<Result<void>>;
  { t.Forward(size) } -> std::same_as<Result<void>>;
  { t.SeekEnd() } -> std::same_as<Result<void>>;
  { t.Reset() } -> std::same_as<void>;
};

//! Serialization limits and type definitions for serio streams.
/*!
 Defines size types and maximum allowed lengths for sequences and strings
 in serio serialization.

 - **SequenceSizeType**: Type used for array and string lengths.
 - **kMaxStringLength**: Maximum allowed string length (1MB).
 - **kMaxArrayLength**: Maximum allowed array length (1MB).

 @see oxygen::serio::Stream, oxygen::serio::AnyStream
*/
namespace limits {
  using SequenceSizeType = uint32_t;
  constexpr SequenceSizeType kMaxStringLength = 1024ULL * 1024; // 1MB
  constexpr SequenceSizeType kMaxArrayLength = 1024ULL * 1024; // 1MB
}

//! Abstract base class for generic byte streams.
/*!
 Provides a virtual interface for reading from and writing to a byte stream. All
 operations are performed in terms of raw bytes, with support for seeking,
 flushing, and querying stream state.

 ### Key Features

 - **Read/Write**: Supports reading and writing raw bytes and spans.
 - **Seeking**: Allows seeking to absolute or relative positions.
 - **State Queries**: Provides size and position queries.
 - **Reset**: Resets the stream to the beginning and clears error states.

 ### Usage Patterns

 Derive from `AnyStream` to implement custom stream types (e.g., memory, file,
 network). Use the interface for generic serialization and deserialization
 routines.

 @note All methods should be noexcept and return `Result` types for error
 handling, except `Reset()` which is guaranteed not to fail.

 @warning Implementations must ensure thread safety if used concurrently.

 @see oxygen::serio::Stream, oxygen::serio::MemoryStream
*/
class AnyStream {
public:
  AnyStream() = default;
  virtual ~AnyStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(AnyStream)
  OXYGEN_DEFAULT_MOVABLE(AnyStream)

  [[nodiscard]] virtual auto Read(std::byte* data, size_t size) noexcept
    -> Result<void>
    = 0;
  [[nodiscard]] virtual auto Write(const std::byte* data, size_t size) noexcept
    -> Result<void>
    = 0;
  [[nodiscard]] virtual auto Write(std::span<const std::byte> data) noexcept
    -> Result<void>
    = 0;
  [[nodiscard]] virtual auto Flush() noexcept -> Result<void> = 0;
  [[nodiscard]] virtual auto Size() const noexcept -> Result<size_t> = 0;
  [[nodiscard]] virtual auto Position() const noexcept -> Result<size_t> = 0;
  [[nodiscard]] virtual auto Seek(size_t pos) noexcept -> Result<void> = 0;
  [[nodiscard]] virtual auto Backward(size_t offset) noexcept -> Result<void>
    = 0;
  [[nodiscard]] virtual auto Forward(size_t offset) noexcept -> Result<void>
    = 0;
  [[nodiscard]] virtual auto SeekEnd() noexcept -> Result<void> = 0;

  //! Reset the stream to the beginning, and clear any previous error or
  //! end-of-stream conditions.
  virtual auto Reset() noexcept -> void = 0;
};

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
  if (n > 0xFF) {
    throw std::out_of_range("byte literal out of range (must be <= 0xFF)");
  }
  return static_cast<std::byte>(n);
}
