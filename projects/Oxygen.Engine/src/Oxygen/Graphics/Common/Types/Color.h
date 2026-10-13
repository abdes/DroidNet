//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <limits>
#include <utility>

namespace oxygen::graphics {

struct Color {
  float r, g, b, a;

  static constexpr float epsilon = std::numeric_limits<float>::epsilon() * 100;

  Color()
    : r(0.F)
    , g(0.F)
    , b(0.F)
    , a(0.F)
  {
  }

  explicit Color(const float c)
    : r(c)
    , g(c)
    , b(c)
    , a(c)
  {
  }
  Color(const float _r, const float _g, const float _b, const float _a)
    : r(_r)
    , g(_g)
    , b(_b)
    , a(_a)
  {
  }

  auto operator==(const Color& other) const -> bool
  {
    auto almost_equal = [](const float x, const float y) -> bool {
      return std::abs(x - y)
        <= epsilon * (std::max)({ 1.0F, std::abs(x), std::abs(y) });
    };
    return almost_equal(r, other.r) && almost_equal(g, other.g)
      && almost_equal(b, other.b) && almost_equal(a, other.a);
  }
  auto operator!=(const Color& other) const -> bool
  {
    return !(*this == other);
  }
};

} // namespace oxygen::graphics
