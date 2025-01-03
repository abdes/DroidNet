//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "Oxygen/Base/Compilers.h"

namespace nostd {

namespace adl_helper {

  template <class T>
  auto as_string(T&& value)
  {
    using std::to_string;
    return to_string(std::forward<T>(value));
  }

} // namespace adl_helper

template <class T>
auto to_string(T&& value)
{
  return adl_helper::as_string(std::forward<T>(value));
}

} // namespace nostd

namespace oxygen {

// Simplify the use of std:: int types and size_t, used so frequently in the
// interfaces.

using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::int8_t;

using std::size_t;

template <typename T>
struct Point {
  T x;
  T y;

  friend auto to_string(Point<T> const& self)
  {
    std::string out = "x: ";
    out.append(nostd::to_string(self.x));
    out.append(", y: ");
    out.append(nostd::to_string(self.y));
    return out;
  }
};

template <typename T>
struct Extent {
  T width;
  T height;

  friend auto to_string(Extent<T> const& self)
  {
    std::string out = "w: ";
    out.append(nostd::to_string(self.width));
    out.append(", h: ");
    out.append(nostd::to_string(self.height));
    return out;
  }
};

template <typename T>
struct Bounds {
  // Upper left corner
  Point<T> origin;
  // Width and height
  Extent<T> extent;

  friend auto to_string(Bounds<T> const& self)
  {
    std::string out = "x: ";
    out.append(nostd::to_string(self.origin.x));
    out.append(", y: ");
    out.append(nostd::to_string(self.origin.y));
    out.append(", w: ");
    out.append(nostd::to_string(self.extent.width));
    out.append(", h: ");
    out.append(nostd::to_string(self.extent.height));
    return out;
  }
};

template <typename T>
struct Motion {
  T dx;
  T dy;

  friend auto to_string(Motion<T> const& self)
  {
    std::string out = "dx: ";
    out.append(nostd::to_string(self.dx));
    out.append(", dy: ");
    out.append(nostd::to_string(self.dy));
    return out;
  }
};

using PixelPosition = Point<int>;
using SubPixelPosition = Point<float>;
using PixelExtent = Extent<int>;
using SubPixelExtent = Extent<float>;
using PixelBounds = Bounds<int>;
using SubPixelBounds = Bounds<float>;
using PixelMotion = Motion<int>;
using SubPixelMotion = Motion<float>;

// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkViewport.html
struct Viewport {
  SubPixelBounds bounds;
  struct
  {
    float min;
    float max;
  } depth;

  friend auto to_string(Viewport const& self)
  {
    std::string out = to_string(self.bounds);
    out.append(", min depth: ");
    out.append(nostd::to_string(self.depth.min));
    out.append(", max depth: ");
    out.append(nostd::to_string(self.depth.max));
    return out;
  }
};

using Duration = std::chrono::microseconds;
using TimePoint = std::chrono::microseconds;

inline auto SecondsToDuration(const float seconds) -> Duration
{
  static constexpr auto micro_seconds_in_second = 1'000'000;
  return Duration(static_cast<uint64_t>(micro_seconds_in_second * seconds));
}

struct Axis1D {
  float x;

  friend auto to_string(Axis1D const& self)
  {
    std::string out = "x: ";
    out.append(nostd::to_string(self.x));
    return out;
  }
};
struct Axis2D {
  float x;
  float y;

  friend auto to_string(Axis2D const& self)
  {
    std::string out = "x: ";
    out.append(nostd::to_string(self.x));
    out.append(", y: ");
    out.append(nostd::to_string(self.y));
    return out;
  }
};

template <class... Ts>
struct Overload : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
Overload(Ts...) -> Overload<Ts...>;

//===----------------------------------------------------------------------===//

// Compile-time endianness detection
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

//===----------------------------------------------------------------------===//

} // namespace oxygen
