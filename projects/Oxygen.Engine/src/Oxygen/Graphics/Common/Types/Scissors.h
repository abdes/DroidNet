//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

struct Scissors {
    int32_t left { 0 };
    int32_t top { 0 };
    int32_t right { 0 };
    int32_t bottom { 0 };

    [[nodiscard]] bool IsValid() const
    {
        return left <= right && top <= bottom;
        // Individual components can be negative if the coordinate system allows,
        // but left should not be greater than right, and top not greater than bottom.
        // Width (right - left) and Height (bottom - top) should be >= 0.
    }
};

OXYGEN_GFX_API auto to_string(const Scissors& scissors) -> std::string;

} // namespace oxygen::graphics
