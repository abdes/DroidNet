//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace oxygen::transforms {

inline [[nodiscard]] auto IsFinite(const glm::vec3& v) noexcept -> bool
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline [[nodiscard]] auto IsFinite(const glm::quat& q) noexcept -> bool
{
  return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z)
    && std::isfinite(q.w);
}

inline [[nodiscard]] auto IsFinite(const glm::mat4& m) noexcept -> bool
{
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
      if (!std::isfinite(m[c][r])) {
        return false;
      }
    }
  }
  return true;
}

} // namespace oxygen::transforms
