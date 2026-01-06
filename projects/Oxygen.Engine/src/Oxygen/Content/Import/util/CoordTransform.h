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

//! Returns the permutation matrix that swaps Y/Z components.
/*!
 The matrix is column-major as expected by ufbx_matrix.

 @return A 4x3 affine matrix representing Y/Z swap.
*/
[[nodiscard]] inline auto SwapYZMatrix() noexcept -> ufbx_matrix
{
  return ufbx_matrix {
    .cols = {
      { 1.0, 0.0, 0.0 },
      { 0.0, 0.0, 1.0 },
      { 0.0, 1.0, 0.0 },
      { 0.0, 0.0, 0.0 },
    },
  };
}

//! Returns ufbx coordinate axes matching Oxygen engine world space.
/*!
 Oxygen engine world conventions (Oxygen/Core/Constants.h):
 - Right-handed
 - Z-up
 - Forward = -Y

 ufbx `front` axis is the "Back" direction (opposite of Forward).

 @return Coordinate axes for ufbx_load_opts::target_axes.
*/
[[nodiscard]] inline auto EngineWorldTargetAxes() noexcept
  -> ufbx_coordinate_axes
{
  return ufbx_coordinate_axes {
    .right = UFBX_COORDINATE_AXIS_POSITIVE_X,
    .up = UFBX_COORDINATE_AXIS_POSITIVE_Z,
    .front = UFBX_COORDINATE_AXIS_POSITIVE_Y,
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

//! Applies Y/Z swap to a transform if enabled in policy.
/*!
 @param policy The coordinate conversion policy.
 @param t The ufbx transform to potentially swap.
 @return The possibly-swapped transform.
*/
[[nodiscard]] inline auto ApplySwapYZIfEnabled(
  const CoordinateConversionPolicy& policy, const ufbx_transform& t)
  -> ufbx_transform
{
  if (!policy.swap_yz_axes) {
    return t;
  }

  // Apply a similarity transform: M' = P * M * P^{-1}.
  // For a pure axis permutation, P^{-1} == P.
  const auto p = SwapYZMatrix();
  const auto m = ufbx_transform_to_matrix(&t);
  const auto pm = ufbx_matrix_mul(&p, &m);
  const auto pmp = ufbx_matrix_mul(&pm, &p);
  return ufbx_matrix_to_transform(&pmp);
}

//! Applies Y/Z swap to a position vector if enabled in policy.
/*!
 @param policy The coordinate conversion policy.
 @param v The position vector.
 @return The possibly-swapped vector.
*/
[[nodiscard]] inline auto ApplySwapYZIfEnabled(
  const CoordinateConversionPolicy& policy, const ufbx_vec3 v) -> ufbx_vec3
{
  if (!policy.swap_yz_axes) {
    return v;
  }

  const auto p = SwapYZMatrix();
  return ufbx_transform_position(&p, v);
}

//! Applies Y/Z swap to a direction vector if enabled in policy.
/*!
 Direction vectors (normals, tangents) use direction transformation
 which excludes translation.

 @param policy The coordinate conversion policy.
 @param v The direction vector.
 @return The possibly-swapped vector.
*/
[[nodiscard]] inline auto ApplySwapYZDirIfEnabled(
  const CoordinateConversionPolicy& policy, const ufbx_vec3 v) -> ufbx_vec3
{
  if (!policy.swap_yz_axes) {
    return v;
  }

  const auto p = SwapYZMatrix();
  return ufbx_transform_direction(&p, v);
}

//! Applies Y/Z swap to a matrix if enabled in policy.
/*!
 @param policy The coordinate conversion policy.
 @param m The matrix.
 @return The possibly-swapped matrix.
*/
[[nodiscard]] inline auto ApplySwapYZIfEnabled(
  const CoordinateConversionPolicy& policy, const ufbx_matrix& m) -> ufbx_matrix
{
  if (!policy.swap_yz_axes) {
    return m;
  }

  const auto p = SwapYZMatrix();
  const auto pm = ufbx_matrix_mul(&p, &m);
  const auto pmp = ufbx_matrix_mul(&pm, &p);
  return pmp;
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
