//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cmath>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace oxygen::interop::module::viewport {

  [[nodiscard]] inline auto NormalizeSafe(const glm::vec3 v,
    const glm::vec3 fallback) noexcept -> glm::vec3 {
    const float len2 = glm::dot(v, v);
    if (len2 <= std::numeric_limits<float>::epsilon()) {
      return fallback;
    }
    return v / std::sqrt(len2);
  }

  [[nodiscard]] inline auto IsFinite(const glm::vec3& v) noexcept -> bool {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  }

} // namespace oxygen::interop::module::viewport

#pragma managed(pop)
