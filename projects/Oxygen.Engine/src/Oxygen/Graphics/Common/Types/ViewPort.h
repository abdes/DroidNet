//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::graphics {

struct ViewPort {
    float top_left_x { 0.f };
    float top_left_y { 0.f };
    float width { 0.f };
    float height { 0.f };
    float min_depth { 0.f };
    float max_depth { 1.f };
};

} // namespace oxygen
