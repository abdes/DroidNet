//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/fbx/ufbx.h>

#include <glm/glm.hpp>

namespace oxygen::content::import::coord {

//! Returns ufbx coordinate axes matching Oxygen engine world space.
/*!
 Oxygen engine world conventions (Oxygen/Core/Constants.h):
 - Right-handed
 - Z-up
 - Forward = -Y

 ufbx `front` axis is the forward direction.

 @return Coordinate axes for ufbx_load_opts::target_axes.
*/
[[nodiscard]] inline auto EngineWorldTargetAxes() noexcept
  -> ufbx_coordinate_axes
{
  return ufbx_coordinate_axes {
    .right = UFBX_COORDINATE_AXIS_POSITIVE_X,
    .up = UFBX_COORDINATE_AXIS_POSITIVE_Z,
    .front = UFBX_COORDINATE_AXIS_NEGATIVE_Y,
  };
}

//! Returns ufbx coordinate axes matching Oxygen engine camera/view space.
/*!
 Oxygen camera/view conventions (Oxygen/Core/Constants.h):
 - view forward = -Z, up = +Y, right = +X

 @return Coordinate axes for ufbx_load_opts::target_camera_axes.
*/
[[nodiscard]] inline auto EngineCameraTargetAxes() noexcept
  -> ufbx_coordinate_axes
{
  return ufbx_coordinate_axes {
    .right = UFBX_COORDINATE_AXIS_POSITIVE_X,
    .up = UFBX_COORDINATE_AXIS_POSITIVE_Y,
    .front = UFBX_COORDINATE_AXIS_POSITIVE_Z,
  };
}

//! Converts ufbx matrix to glm::mat4.
/*!
 ufbx_matrix is an affine 4x3 matrix in column-major form.
 cols[0..2] are basis vectors, cols[3] is translation.

 @param m The ufbx matrix.
 @return The equivalent glm::mat4.
*/
[[nodiscard]] inline auto ToGlmMat4(const ufbx_matrix& m) noexcept -> glm::mat4
{
  glm::mat4 out(1.0F);
  out[0] = glm::vec4(static_cast<float>(m.cols[0].x),
    static_cast<float>(m.cols[0].y), static_cast<float>(m.cols[0].z), 0.0F);
  out[1] = glm::vec4(static_cast<float>(m.cols[1].x),
    static_cast<float>(m.cols[1].y), static_cast<float>(m.cols[1].z), 0.0F);
  out[2] = glm::vec4(static_cast<float>(m.cols[2].x),
    static_cast<float>(m.cols[2].y), static_cast<float>(m.cols[2].z), 0.0F);
  out[3] = glm::vec4(static_cast<float>(m.cols[3].x),
    static_cast<float>(m.cols[3].y), static_cast<float>(m.cols[3].z), 1.0F);
  return out;
}

//! Computes target unit meters for ufbx from policy.
/*!
 @param policy The coordinate conversion policy.
 @return The target unit in meters, or nullopt to preserve source units.
*/
[[nodiscard]] inline auto ComputeTargetUnitMeters(
  const CoordinateConversionPolicy& policy) noexcept -> std::optional<ufbx_real>
{
  switch (policy.unit_normalization) {
  case UnitNormalizationPolicy::kNormalizeToMeters:
    return 1.0;
  case UnitNormalizationPolicy::kPreserveSource:
    return std::nullopt;
  case UnitNormalizationPolicy::kApplyCustomFactor: {
    if (!(policy.custom_unit_scale > 0.0F)) {
      return std::nullopt;
    }
    return static_cast<ufbx_real>(1.0 / policy.custom_unit_scale);
  }
  }

  return std::nullopt;
}

} // namespace oxygen::content::import::coord
