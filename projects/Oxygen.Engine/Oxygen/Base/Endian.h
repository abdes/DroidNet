//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <cstdlib>
#include <type_traits>

#include "Oxygen/Base/Compilers.h"

namespace oxygen {

//! Compile-time endianness detection
inline bool IsLittleEndian()
{
  constexpr uint32_t value = 0x01234567;
  return *reinterpret_cast<const uint8_t*>(&value) == 0x67;
}

#if defined(OXYGEN_MSVC_VERSION)
inline uint16_t ByteSwap16(const uint16_t value) { return _byteswap_ushort(value); }
inline uint32_t ByteSwap32(const uint32_t value) { return _byteswap_ulong(value); }
inline uint64_t ByteSwap64(const uint64_t value) { return _byteswap_uint64(value); }
#elif defined(OXYGEN_APPLE)
#include <libkern/OSByteOrder.h>
inline uint16_t ByteSwap16(const uint16_t value) { return OSSwapInt16(value); }
inline uint32_t ByteSwap32(const uint32_t value) { return OSSwapInt32(value); }
inline uint64_t ByteSwap64(const uint64_t value) { return OSSwapInt64(value); }
#elif defined(OXYGEN_CLANG_VERSION) || defined(OXYGEN_GCC_VERSION)
inline uint16_t ByteSwap16(const uint16_t value) { return __builtin_bswap16(value); }
inline uint32_t ByteSwap32(const uint32_t value) { return __builtin_bswap32(value); }
inline uint64_t ByteSwap64(const uint64_t value) { return __builtin_bswap64(value); }
#else
// Fallback implementation
inline uint16_t ByteSwap16(const uint16_t value)
{
  return (value << 8) | (value >> 8);
}
inline uint32_t ByteSwap32(const uint32_t value)
{
  return ((value & 0xFF000000) >> 24)
    | ((value & 0x00FF0000) >> 8)
    | ((value & 0x0000FF00) << 8)
    | ((value & 0x000000FF) << 24);
}
inline uint64_t ByteSwap64(const uint64_t value)
{
  return ((value & 0xFF00000000000000ULL) >> 56)
    | ((value & 0x00FF000000000000ULL) >> 40)
    | ((value & 0x0000FF0000000000ULL) >> 24)
    | ((value & 0x000000FF00000000ULL) >> 8)
    | ((value & 0x00000000FF000000ULL) << 8)
    | ((value & 0x0000000000FF0000ULL) << 24)
    | ((value & 0x000000000000FF00ULL) << 40)
    | ((value & 0x00000000000000FFULL) << 56);
}
#endif

template <typename T>
constexpr T ByteSwap(T value) noexcept
{
  static_assert(std::is_trivially_copyable_v<T>);
  if constexpr (sizeof(T) == 1)
    return value;
  if constexpr (sizeof(T) == 2) {
    return static_cast<T>(ByteSwap16(static_cast<uint16_t>(value)));
  }
  if constexpr (sizeof(T) == 4) {
    return static_cast<T>(ByteSwap32(static_cast<uint32_t>(value)));
  }
  if constexpr (sizeof(T) == 8) {
    return static_cast<T>(ByteSwap64(static_cast<uint64_t>(value)));
  }
  OXYGEN_UNREACHABLE_RETURN(value);
}

} // namespace oxygen
