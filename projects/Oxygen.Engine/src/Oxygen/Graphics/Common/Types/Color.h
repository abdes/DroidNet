//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::graphics {

struct Color {
    float r, g, b, a;

    Color()
        : r(0.f)
        , g(0.f)
        , b(0.f)
        , a(0.f)
    {
    }
    Color(float c)
        : r(c)
        , g(c)
        , b(c)
        , a(c)
    {
    }
    Color(float _r, float _g, float _b, float _a)
        : r(_r)
        , g(_g)
        , b(_b)
        , a(_a)
    {
    }

    bool operator==(const Color& other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }
    bool operator!=(const Color& other) const { return !(*this == other); }
};

} // namespace oxygen::graphics
