// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace nostd {

  namespace adl_helper {

    template <class T>
    auto as_string(T&& value) {
      using std::to_string;
      return to_string(std::forward<T>(value));
    }

  }  // namespace adl_helper

  template <class T>
  auto to_string(T&& value) {
    return adl_helper::as_string(std::forward<T>(value));
  }

}  // namespace nostd

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
  struct Point
  {
    T x;
    T y;

    friend auto to_string(Point<T> const& self) {
      std::string out = "x: ";
      out.append(nostd::to_string(self.x));
      out.append(", y: ");
      out.append(nostd::to_string(self.y));
      return out;
    }
  };

  template <typename T>
  struct Extent
  {
    T width;
    T height;

    friend auto to_string(Extent<T> const& self) {
      std::string out = "w: ";
      out.append(nostd::to_string(self.width));
      out.append(", h: ");
      out.append(nostd::to_string(self.height));
      return out;
    }
  };

  template <typename T>
  struct Bounds
  {
    // Upper left corner
    Point<T> origin;
    // Width and height
    Extent<T> extent;

    friend auto to_string(Bounds<T> const& self) {
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
  struct Motion
  {
    T dx;
    T dy;

    friend auto to_string(Motion<T> const& self) {
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
  struct Viewport
  {
    SubPixelBounds bounds;
    struct
    {
      float min;
      float max;
    } depth;

    friend auto to_string(Viewport const& self) {
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

  inline auto SecondsToDuration(const float seconds) -> Duration {
    static constexpr auto micro_seconds_in_second = 1'000'000;
    return Duration(static_cast<uint64_t>(micro_seconds_in_second * seconds));
  }

  struct Axis1D
  {
    float x;

    friend auto to_string(Axis1D const& self) {
      std::string out = "x: ";
      out.append(nostd::to_string(self.x));
      return out;
    }
  };
  struct Axis2D
  {
    float x;
    float y;

    friend auto to_string(Axis2D const& self) {
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

}  // namespace oxygen
