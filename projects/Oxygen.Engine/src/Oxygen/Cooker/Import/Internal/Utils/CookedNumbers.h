//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace oxygen::content::import::internal {

//! An authored number narrowed to the float a cooked record stores, or nothing
//! for one a float cannot hold: not finite, or beyond the float range.
/*!
 A nonzero value too small for a float becomes zero (or a subnormal); a rule
 that needs a positive value checks the float it gets, not the authored one.
*/
[[nodiscard]] inline auto ToCookedFloat(const double value) noexcept
  -> std::optional<float>
{
  if (!std::isfinite(value)
    || std::abs(value)
      > static_cast<double>(std::numeric_limits<float>::max())) {
    return std::nullopt;
  }
  return static_cast<float>(value);
}

//! How far the squared length of an authored rotation may be from one.
inline constexpr float kUnitQuaternionTolerance = 1.0e-3F;

//! The squared length of the quaternion `[x, y, z, w]`.
[[nodiscard]] inline auto LengthSquared(const std::array<float, 4>& q) noexcept
  -> float
{
  return (q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]);
}

//! Whether the authored rotation `[x, y, z, w]` is a unit quaternion, within
//! `kUnitQuaternionTolerance`.
[[nodiscard]] inline auto IsUnitQuaternion(
  const std::array<float, 4>& q) noexcept -> bool
{
  const auto length_squared = LengthSquared(q);
  return std::isfinite(length_squared)
    && std::abs(length_squared - 1.0F) <= kUnitQuaternionTolerance;
}

} // namespace oxygen::content::import::internal
