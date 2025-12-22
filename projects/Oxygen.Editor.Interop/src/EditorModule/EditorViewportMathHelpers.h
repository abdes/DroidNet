//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cmath>
#include <limits>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

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

  [[nodiscard]] inline auto LookRotationFromPositionToTarget(
    const glm::vec3& position,
    const glm::vec3& target_position,
    const glm::vec3& up_direction) noexcept -> glm::quat {
    const glm::vec3 forward =
      NormalizeSafe(target_position - position, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 right =
      NormalizeSafe(glm::cross(forward, up_direction), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 up = glm::cross(right, forward);

    glm::mat4 look_matrix(1.0f);
    look_matrix[0] = glm::vec4(right, 0.0f);
    look_matrix[1] = glm::vec4(up, 0.0f);
    look_matrix[2] = glm::vec4(-forward, 0.0f);

    return glm::quat_cast(look_matrix);
  }

  [[nodiscard]] inline auto LookRotationFromForwardUp(const glm::vec3 forward,
    const glm::vec3 up_direction) noexcept -> glm::quat {
    const glm::vec3 f = NormalizeSafe(forward, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 r =
      NormalizeSafe(glm::cross(f, up_direction), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 u = glm::cross(r, f);

    glm::mat4 look_matrix(1.0f);
    look_matrix[0] = glm::vec4(r, 0.0f);
    look_matrix[1] = glm::vec4(u, 0.0f);
    look_matrix[2] = glm::vec4(-f, 0.0f);
    return glm::quat_cast(look_matrix);
  }

} // namespace oxygen::interop::module::viewport

#pragma managed(pop)
