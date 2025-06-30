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

#include <Oxygen/Base/Result.h>

namespace oxygen::serio {

//! Concept to specify a stream that can be written to and read from.
template <typename T>
concept Stream = requires(T t, const std::byte* cdata, std::byte* data,
  std::span<const std::byte> sdata, size_t size) {
  { t.write(cdata, size) } -> std::same_as<Result<void>>;
  { t.write(cdata, size) } -> std::same_as<Result<void>>;
  { t.read(data, size) } -> std::same_as<Result<void>>;
  { t.flush() } -> std::same_as<Result<void>>;
  { t.position() } -> std::same_as<Result<size_t>>;
  { t.seek(size) } -> std::same_as<Result<void>>;
  { t.backward(size) } -> std::same_as<Result<void>>;
  { t.forward(size) } -> std::same_as<Result<void>>;
  { t.seek_end() } -> std::same_as<Result<void>>;
  { t.size() } -> std::same_as<Result<size_t>>;
};

namespace limits {
  using SequenceSizeType = uint32_t;
  constexpr SequenceSizeType kMaxStringLength = 1024ULL * 1024; // 1MB
  constexpr SequenceSizeType kMaxArrayLength = 1024ULL * 1024; // 1MB
}

} // namespace oxygen::serio

//! User-defined literal operator for `std::byte` literals.
/*!
 For character literals: 'x'_b
*/
constexpr std::byte operator"" _b(char c) noexcept
{
  return static_cast<std::byte>(c);
}

//! User-defined literal operator for `std::byte` literals.
/*!
 For integer literals: 0xFF_b

 @throw std::out_of_range if the value exceeds the range of a byte (0x00 to
 0xFF). Use compile time `consteval` when availabled. Fallas back to runtime
 checks for earlier standards.

 @note The C++ standard mandates that user-defined literals can only be defined
 for unsigned long long, so we use that type for the literal operator.
*/
constexpr std::byte operator"" _b(unsigned long long n)
{
  // Compile-time check: only allow values that fit in a byte
#if defined(__cpp_consteval) // C++20 consteval
  if (n > 0xFF) {
    throw "Byte literal out of range (must be <= 0xFF)";
  }
#else
  if (n > 0xFF) {
    throw std::out_of_range("Byte literal out of range (must be <= 0xFF)");
  }
#endif
  return static_cast<std::byte>(n);
}
