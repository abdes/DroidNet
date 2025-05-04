//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::graphics {

struct Scissors {
    Scissors() = default;
    Scissors(
        const int32_t left, const int32_t top,
        const int32_t right, const int32_t bottom)
        : left(left)
        , top(top)
        , right(right)
        , bottom(bottom)
    {
    }
    int32_t left { 0 };
    int32_t top { 0 };
    int32_t right { 0 };
    int32_t bottom { 0 };
};

} // namespace oxygen
