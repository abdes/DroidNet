//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <fmt/format.h>

#include <Oxygen/Core/ViewPort.h>

auto oxygen::to_string(const ViewPort& viewport) -> std::string
{
  return fmt::format(
    "ViewPort{{tl.x={}, tl.y={}, w={}, h={}, min_depth={}, max_depth={}}}",
    viewport.top_left_x, viewport.top_left_y, viewport.width, viewport.height,
    viewport.min_depth, viewport.max_depth);
}
