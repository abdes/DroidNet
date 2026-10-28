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

#include <Oxygen/Data/SceneRecordFields.h>

namespace oxygen::interop::rotation {

//! The X-Y-Z authoring convention (R = Rz * Ry * Rx), which the scene
//! schema's rotation encoding owns.
inline auto ToQuaternion(const glm::vec3& degrees) -> glm::quat
{
  const auto rotation = oxygen::data::pak::world::EulerXyzDegreesToQuaternion(
    { degrees.x, degrees.y, degrees.z });
  return { rotation[3], rotation[0], rotation[1], rotation[2] };
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
  // sin(Y) = -R20 of R = Rz * Ry * Rx.
  const auto test = std::clamp(2.0F * (q.w * q.y - q.x * q.z), -1.0F, 1.0F);
  glm::vec3 angles;
  if (std::abs(test) >= 0.99999F) {
    // X and Z turn about one axis: X = 0, then R01 = -sin(Z), R11 = cos(Z).
    const auto r01 = 2.0F * (q.x * q.y - q.w * q.z);
    const auto r11 = 1.0F - 2.0F * (q.x * q.x + q.z * q.z);
    angles = { 0.0F, std::copysign(glm::half_pi<float>(), test),
      std::atan2(-r01, r11) };
  } else {
    angles = { std::atan2(2.0F * (q.y * q.z + q.w * q.x),
                 1.0F - 2.0F * (q.x * q.x + q.y * q.y)),
      std::asin(test),
      std::atan2(2.0F * (q.x * q.y + q.w * q.z),
        1.0F - 2.0F * (q.y * q.y + q.z * q.z)) };
  }
  angles = glm::degrees(angles);
  const glm::vec3 alternative { NormalizeAngle(angles.x + 180.0F),
    angles.y >= 0.0F ? 180.0F - angles.y : -180.0F - angles.y,
    NormalizeAngle(angles.z + 180.0F) };
  const auto cost = std::abs(angles.x) + std::abs(angles.z);
  const auto alternative_cost = std::abs(alternative.x) + std::abs(alternative.z);
  if ((std::abs(alternative.y) < std::abs(angles.y)
        && alternative_cost <= cost + 10.0F)
    || alternative_cost < cost - 10.0F) {
    return alternative;
  }
  return angles;
}

} // namespace oxygen::interop::rotation

#pragma managed(pop)
