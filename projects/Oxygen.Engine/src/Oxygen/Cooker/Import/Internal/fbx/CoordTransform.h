//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>

namespace oxygen::content::import::coord {

//! Returns ufbx coordinate axes matching Oxygen engine world space.
/*!
 Oxygen engine world conventions (Oxygen/Core/Constants.h):
 - Right-handed
 - Z-up
 - Forward = -Y

 ufbx's `front` axis is the direction an asset's front faces, toward a viewer
 looking at it: Oxygen's forward, -Y.

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

//! Returns ufbx coordinate axes matching the local frame of an Oxygen node.
/*!
 Every Oxygen node, cameras and lights included, faces its local forward -Y
 with up +Z and right -X (`space::node` in Oxygen/Core/Constants.h). ufbx's
 `front` axis points opposite to where a camera looks or a light shines.

 @return Coordinate axes for ufbx_load_opts::target_camera_axes and
   ufbx_load_opts::target_light_axes.
*/
[[nodiscard]] inline auto EngineNodeTargetAxes() noexcept
  -> ufbx_coordinate_axes
{
  return ufbx_coordinate_axes {
    .right = UFBX_COORDINATE_AXIS_NEGATIVE_X,
    .up = UFBX_COORDINATE_AXIS_POSITIVE_Z,
    .front = UFBX_COORDINATE_AXIS_POSITIVE_Y,
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
  out[0] = glm::vec4(m.cols[0].x, m.cols[0].y, m.cols[0].z, 0.0F);
  out[1] = glm::vec4(m.cols[1].x, m.cols[1].y, m.cols[1].z, 0.0F);
  out[2] = glm::vec4(m.cols[2].x, m.cols[2].y, m.cols[2].z, 0.0F);
  out[3] = glm::vec4(m.cols[3].x, m.cols[3].y, m.cols[3].z, 1.0F);
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
    return 1.0F;
  case UnitNormalizationPolicy::kPreserveSource:
    return std::nullopt;
  case UnitNormalizationPolicy::kApplyCustomFactor: {
    if (!(policy.unit_scale > 0.0F)) {
      return std::nullopt;
    }
    return 1.0F / policy.unit_scale;
  }
  }

  return std::nullopt;
}

} // namespace oxygen::content::import::coord
