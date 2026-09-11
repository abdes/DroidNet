//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace oxygen::interop::rotation {

//! Matches the editor's Y-X-Z authoring convention (X pitch, Y yaw, Z roll).
inline auto ToQuaternion(const glm::vec3& degrees) -> glm::quat
{
  const auto radians = glm::radians(degrees);
  return glm::angleAxis(radians.y, glm::vec3(0.0F, 1.0F, 0.0F))
    * glm::angleAxis(radians.x, glm::vec3(1.0F, 0.0F, 0.0F))
    * glm::angleAxis(radians.z, glm::vec3(0.0F, 0.0F, 1.0F));
}

//! Normalizes one authored angle to [-180, 180).
inline auto NormalizeAngle(float degrees) -> float
{
  auto result = std::fmod(degrees, 360.0F);
  if (result >= 180.0F) {
    result -= 360.0F;
  }
  if (result < -180.0F) {
    result += 360.0F;
  }
  return result;
}

//! Uses the same gimbal-lock and beyond-90-degree representation as TransformConverter.
inline auto ToEulerDegrees(glm::quat rotation) -> glm::vec3
{
  if (glm::dot(rotation, rotation) < 1.0e-6F) {
    return glm::vec3(0.0F);
  }
  const auto q = glm::normalize(rotation);
  const auto test = std::clamp(2.0F * (q.w * q.x - q.y * q.z), -1.0F, 1.0F);
  glm::vec3 angles;
  if (std::abs(test) >= 0.99999F) {
    const auto r01 = 2.0F * (q.x * q.y - q.w * q.z);
    const auto r00 = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
    angles = { std::copysign(glm::half_pi<float>(), test),
      test > 0.0F ? std::atan2(r01, r00) : std::atan2(-r01, r00), 0.0F };
  } else {
    angles = { std::asin(test),
      std::atan2(2.0F * (q.w * q.y + q.x * q.z),
        1.0F - 2.0F * (q.x * q.x + q.y * q.y)),
      std::atan2(2.0F * (q.w * q.z + q.x * q.y),
        1.0F - 2.0F * (q.x * q.x + q.z * q.z)) };
  }
  angles = glm::degrees(angles);
  const glm::vec3 alternative {
    angles.x >= 0.0F ? 180.0F - angles.x : -180.0F - angles.x,
    NormalizeAngle(angles.y + 180.0F), NormalizeAngle(angles.z + 180.0F) };
  const auto cost = std::abs(angles.y) + std::abs(angles.z);
  const auto alternative_cost = std::abs(alternative.y) + std::abs(alternative.z);
  if ((std::abs(alternative.x) < std::abs(angles.x)
        && alternative_cost <= cost + 10.0F)
    || alternative_cost < cost - 10.0F) {
    return alternative;
  }
  return angles;
}

} // namespace oxygen::interop::rotation

#pragma managed(pop)
