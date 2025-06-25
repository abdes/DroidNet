//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Types/Geometry.h>

namespace oxygen {

// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkViewport.html
struct Viewport {
  SubPixelBounds bounds;
  struct {
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

} // namespace oxygen
